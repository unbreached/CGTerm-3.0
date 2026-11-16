#include <sys/types.h>
#if defined(__WIN32__) || defined(WINDOWS)
# ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0501   /* getaddrinfo / getnameinfo */
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_thread.h>
#include "net.h"
#include "config.h"
#include "gfx.h"
#include "ansi.h"

#if defined(__WIN32__) || defined(WINDOWS)
/* Windows (socklen_t comes from ws2tcpip.h) */
#define WOULDBLOCK() (WSAGetLastError() == WSAEWOULDBLOCK)
#define CLOSESOCKET(s)  closesocket(s)
#else
/* Unix */
typedef int SOCKET;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#define WOULDBLOCK() (errno == EWOULDBLOCK || errno == EAGAIN)
#define CLOSESOCKET(s)  close(s)
#endif

/* Linux: suppress SIGPIPE per call; macOS/BSD use SO_NOSIGPIPE at connect time;
 * both additionally ignore SIGPIPE process-wide (see net_connect). */
#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

#define ISCONNECTED() (conn != INVALID_SOCKET)


#define BUFSIZE 1024
SOCKET conn = INVALID_SOCKET;
#if defined(__WIN32__) || defined(WINDOWS)
int winsockstarted = 0;
#endif
unsigned char buffer[BUFSIZE];
unsigned char *bufptr;
signed int buflen;

void (*net_status)(int, char *);

/* Called whenever a connection starts or ends, so the application can drop
 * anything still queued for the old peer (paste queue, login script). Left
 * NULL by programs that have no such state (cgchat, the tests). */
void (*net_session_reset_hook)(void) = NULL;

/* Persistent Telnet IAC parser state, so an IAC sequence split across
 * recv() boundaries resumes correctly on the next net_receive() call
 * instead of desynchronising and leaking the command byte as data. */
enum { TN_DATA = 0, TN_IAC, TN_OPT, TN_SB_OPT, TN_SB_DATA };
static int tn_state = TN_DATA;
static int tn_cmd = 0;          /* WILL/WONT/DO/DONT while in TN_OPT */
static int tn_sb_opt = 0;       /* subnegotiation option while in TN_SB_* */
static int tn_sb_prev = 0;      /* previous byte, for detecting IAC SE */
static int tn_sb_iter = 0;      /* subnegotiation byte counter (abuse cap) */
static int net_telnet_seen = 0; /* peer negotiated Telnet -> escape outgoing 0xFF,
                                   un-escape incoming IAC IAC (also in raw mode) */
static int tn_pushback = -1;    /* one byte held back for the next read (raw-peer 0xFF) */
static int tn_data_bytes = 0;   /* data bytes delivered since connect (detection window) */
static int tn_replies = 0;      /* negotiation replies sent this connection (flood cap) */
static int tn_sb_first = -1;    /* first data byte of the current subnegotiation */

/* A Telnet server negotiates at the very start of the connection. A raw
 * C64 board never does, but its PETSCII art can contain pi (0xFF) followed
 * by a graphics byte 0xFA-0xFE, which looks exactly like IAC SB/WILL/WONT/
 * DO/DONT. Honouring that latched Telnet mode for the rest of the session
 * and corrupted every later binary transfer (outgoing 0xFF doubled,
 * incoming 0xFF pairs eaten). So in PETSCII mode a first negotiation is
 * only believed inside this many data bytes from connect. */
#define TN_DETECT_WINDOW 512
#define TN_MAX_REPLIES 256

static int tn_may_start(void) {
  return net_telnet_seen || cfg_termmode == 1 || tn_data_bytes < TN_DETECT_WINDOW;
}

static void net_reset_telnet(void) {
  tn_state = TN_DATA;
  tn_cmd = tn_sb_opt = tn_sb_prev = tn_sb_iter = 0;
  net_telnet_seen = 0;
  tn_pushback = -1;
  tn_data_bytes = 0;
  tn_replies = 0;
  tn_sb_first = -1;
}

static signed int net_raw_byte(void);

/* Write n bytes, coping with the non-blocking socket: a short write or
 * EAGAIN (peer not reading, kernel buffer full) waits for writability
 * instead of silently dropping the bytes - a dropped byte inside a Punter
 * or XMODEM block corrupts the transfer. Hard errors disconnect. */
static int net_write(const unsigned char *p, int n) {
  int total = 0;

  if (!ISCONNECTED()) {
    return 0;
  }
  while (total < n) {
    int r = (int)send(conn, (const char *)p + total, n - total, SEND_FLAGS);
    if (r > 0) {
      total += r;
      continue;
    }
    if (r < 0 && (WOULDBLOCK() || errno == EINTR)) {
      fd_set wfds;
      struct timeval tv;
      FD_ZERO(&wfds);
      FD_SET(conn, &wfds);
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      if (select((int)conn + 1, NULL, &wfds, NULL, &tv) > 0) {
        continue;
      }
      if (net_status) net_status(2, "Send timed out");
      net_disconnect();
      return 0;
    }
    if (net_status) net_status(2, "Send failed");
    net_disconnect();
    return 0;
  }
  return 1;
}


/* Proactive negotiation state: which options we already offered, so the
 * server's DO/WILL answer does not make us repeat the offer. */
static int tn_offered_ttype = 0, tn_offered_naws = 0, tn_offered_sga = 0;
static int tn_naws_agreed = 0;

static void net_send_naws_sb(void) {
  unsigned char naws[9] = {0xFF, 0xFA, 0x1F,
    0, (unsigned char)cfg_columns, 0, (unsigned char)cfg_rows,
    0xFF, 0xF0};
  net_write(naws, 9);
}

/* Called after a 40/80 column switch: tell a Telnet board the new size. */
void net_send_naws(void) {
  if (ISCONNECTED() && net_telnet_seen && tn_naws_agreed) {
    net_send_naws_sb();
  }
}

static void net_address_string(const struct sockaddr *sa, socklen_t len, char *out, size_t outsz) {
  if (getnameinfo(sa, len, out, (socklen_t)outsz, NULL, 0, NI_NUMERICHOST) != 0) {
    snprintf(out, outsz, "?");
  }
}

/* Text for a socket error. Winsock does not set errno and strerror() knows
 * nothing about WSA codes, so Windows users saw "No error" for a refused
 * connection. */
static const char *net_errstr(int err) {
#if defined(__WIN32__) || defined(WIN32) || defined(WINDOWS)
  static char buf[48];
  switch (err) {
  case WSAECONNREFUSED: return "Connection refused";
  case WSAETIMEDOUT:    return "Connection timed out";
  case WSAENETUNREACH:  return "Network unreachable";
  case WSAEHOSTUNREACH: return "Host unreachable";
  case WSAECONNRESET:   return "Connection reset by peer";
  case WSAECONNABORTED: return "Connection aborted";
  case WSAENETDOWN:     return "Network is down";
  case 0:               return "Connection failed";
  default:
    snprintf(buf, sizeof(buf), "Socket error %d", err);
    return buf;
  }
#else
  return err ? strerror(err) : "Connection failed";
#endif
}

static int net_lasterr(void) {
#if defined(__WIN32__) || defined(WIN32) || defined(WINDOWS)
  return WSAGetLastError();
#else
  return errno;
#endif
}

/* ---- Hostname lookup on a worker thread ----
 * getaddrinfo() can block for the whole resolver timeout (tens of seconds
 * with a dead DNS server). Run on the UI thread it froze the window with no
 * way to cancel, and again on every automatic redial. The job is heap
 * allocated and owned by whoever finishes last: if the user cancels, the
 * thread frees it when the lookup finally returns. */
typedef struct {
  char host[256];
  char port[16];
  struct addrinfo hints;
  struct addrinfo *result;
  int rc;
  int done;
  int abandoned;
  SDL_mutex *lock;
} ResolveJob;

static int net_resolve_thread(void *arg) {
  ResolveJob *job = (ResolveJob *)arg;
  struct addrinfo *res = NULL;
  int rc = getaddrinfo(job->host, job->port, &job->hints, &res);
  int abandoned;

  SDL_mutexP(job->lock);
  job->rc = rc;
  job->result = res;
  job->done = 1;
  abandoned = job->abandoned;
  SDL_mutexV(job->lock);
  if (abandoned) {
    if (res) freeaddrinfo(res);
    SDL_DestroyMutex(job->lock);
    free(job);
  }
  return 0;
}

/* Returns 0 with *out set, 1 on failure, 2 if the user pressed ESC. */
static int net_resolve(const char *host, const char *port, const struct addrinfo *hints, struct addrinfo **out) {
  ResolveJob *job = calloc(1, sizeof(*job));
  SDL_Thread *th;
  int rc;

  *out = NULL;
  if (!job) return 1;
  snprintf(job->host, sizeof(job->host), "%s", host);
  snprintf(job->port, sizeof(job->port), "%s", port);
  job->hints = *hints;
  job->lock = SDL_CreateMutex();
  th = job->lock ? SDL_CreateThread(net_resolve_thread, job) : NULL;
  if (!th) {
    /* no thread available: fall back to the blocking call */
    if (job->lock) SDL_DestroyMutex(job->lock);
    free(job);
    return (getaddrinfo(host, port, hints, out) != 0 || *out == NULL) ? 1 : 0;
  }

  for (;;) {
    SDL_Event ev;
    int done, cancel = 0;

    SDL_mutexP(job->lock);
    done = job->done;
    SDL_mutexV(job->lock);
    if (done) break;

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) cancel = 1;
    }
    if (cancel) {
      SDL_mutexP(job->lock);
      if (!job->done) {
        job->abandoned = 1;      /* the thread cleans up when it returns */
        SDL_mutexV(job->lock);
        return 2;
      }
      SDL_mutexV(job->lock);
      break;
    }
    gfx_vbl();
    SDL_Delay(20);
  }

  SDL_WaitThread(th, NULL);
  rc = job->rc;
  *out = job->result;
  SDL_DestroyMutex(job->lock);
  free(job);
  if (rc != 0 || *out == NULL) {
    if (*out) { freeaddrinfo(*out); *out = NULL; }
    return 1;
  }
  return 0;
}

/* Non-blocking connect to one address with a 10 s limit and ESC to cancel.
 * Returns 0 connected (conn set), 1 failed (conn closed), 2 cancelled. */
static int net_try_connect(const struct addrinfo *ai) {
  char addrstr[64];
  char msg[128];

  if ((conn = socket(ai->ai_family, SOCK_STREAM, 0)) == INVALID_SOCKET) {
    return 1;
  }
#ifdef SO_NOSIGPIPE
  {
    int one = 1;
    setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, (const void *)&one, sizeof(one));
  }
#endif
  net_address_string(ai->ai_addr, (socklen_t)ai->ai_addrlen, addrstr, sizeof(addrstr));
  snprintf(msg, sizeof(msg), "Connecting to %s...", addrstr);
  if (net_status) net_status(0, msg);

  {
#if defined(__WIN32__) || defined(WIN32) || defined(WINDOWS)
    u_long nb = 1;
    ioctlsocket(conn, FIONBIO, &nb);
#else
    fcntl(conn, F_SETFL, O_NONBLOCK);
#endif
  }

  if (connect(conn, ai->ai_addr, (socklen_t)ai->ai_addrlen) < 0) {
#if defined(__WIN32__) || defined(WIN32) || defined(WINDOWS)
    int inprogress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
    int inprogress = (errno == EINPROGRESS || errno == EINTR);
#endif
    if (!inprogress) {
      /* Immediate failure (network unreachable, no route, ...) */
      if (net_status) net_status(2, (char *)net_errstr(net_lasterr()));
      CLOSESOCKET(conn);
      conn = INVALID_SOCKET;
      return 1;
    }
  }

  /* Poll with select() — 10 second timeout, check ESC every 200ms */
  {
    fd_set wfds, efds;
    struct timeval tv;
    int elapsed = 0;

    while (elapsed < 10000) {
      SDL_Event ev;

      FD_ZERO(&wfds);
      FD_SET(conn, &wfds);
      FD_ZERO(&efds);
      FD_SET(conn, &efds);   /* Winsock reports a failed connect here */
      tv.tv_sec = 0;
      tv.tv_usec = 200000;  /* 200ms */

      if (select((int)conn + 1, NULL, &wfds, &efds, &tv) > 0) {
        int err = 0;
        socklen_t errlen = sizeof(err);
        getsockopt(conn, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen);
        if (err == 0 && !FD_ISSET(conn, &efds)) {
          return 0;
        }
        /* Connect failed: say why (refused, unreachable, ...) */
        if (net_status) net_status(2, (char *)net_errstr(err));
        CLOSESOCKET(conn);
        conn = INVALID_SOCKET;
        return 1;
      }

      elapsed += 200;

      /* Pump SDL events — check for ESC to cancel */
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) exit(0);
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
          CLOSESOCKET(conn);
          conn = INVALID_SOCKET;
          if (net_status) net_status(2, "Cancelled");
          return 2;
        }
      }

      /* Update screen so it doesn't look frozen */
      gfx_vbl();
    }
  }

  CLOSESOCKET(conn);
  conn = INVALID_SOCKET;
  if (net_status) net_status(2, "Connection timed out");
  return 1;
}


int net_connect(const char *host, int port, void (*status)(int, char *)) {
  struct addrinfo hints, *result = NULL, *ai;
  char port_str[16];
  int pass, rc = 1;
#if defined(__WIN32__) || defined(WINDOWS)
  WSADATA wsaData;
#endif

  /* Reject NULL/empty host before any string indexing. */
  if (host == NULL || host[0] == 0) {
    if (status) status(2, "No host specified");
    return(1);
  }
  /* Never leak an existing connection (modem pre-flight fallbacks, redial). */
  if (ISCONNECTED()) {
    net_disconnect();
  }
  if (net_session_reset_hook) net_session_reset_hook();
#if !defined(__WIN32__) && !defined(WINDOWS)
  /* A write to a socket the peer already reset must not kill the process. */
  signal(SIGPIPE, SIG_IGN);
#endif
#if defined(__WIN32__) || defined(WINDOWS)
  if (!winsockstarted) {
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      printf("WinSock startup failed\n");
      return(1);
    }
    atexit((void (*)(void))WSACleanup);
    winsockstarted = 1;
  }
#endif
  net_status = status;

  buflen = 0;
  bufptr = buffer;
  net_reset_telnet();
  tn_offered_ttype = tn_offered_naws = tn_offered_sga = 0;
  tn_naws_agreed = 0;
  ansi_reset();   /* fresh escape-parser state for the new session */

  /* Resolve every address of the host (IPv4 and IPv6). Literal addresses
   * resolve too, so "c64node1" and "::1" both work. */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
  hints.ai_flags = AI_ADDRCONFIG;   /* only families we actually have */
#endif
  snprintf(port_str, sizeof(port_str), "%d", port);
  if (net_status) net_status(0, "Resolving...");
  {
    int rr = net_resolve(host, port_str, &hints, &result);
    if (rr != 0) {
      if (net_status) net_status(2, rr == 2 ? "Cancelled" : "Hostname resolution failed");
      return(1);
    }
  }

  /* IPv4 first: most boards are IPv4-only and a stale AAAA record is a
   * common way to wait out a 10 s timeout for nothing. Then IPv6. */
  for (pass = 0; pass < 2 && rc != 0; pass++) {
    int want = (pass == 0) ? AF_INET : AF_INET6;
    for (ai = result; ai && rc != 0; ai = ai->ai_next) {
      if (ai->ai_family != want) continue;
      rc = net_try_connect(ai);
      if (rc == 2) {   /* ESC */
        freeaddrinfo(result);
        return(1);
      }
    }
  }
  freeaddrinfo(result);
  if (rc != 0) {
    return(1);
  }

  /* Keep non-blocking for recv */
  if (net_status) net_status(1, "Connected");
  cfg_log_connection(host, port);
  {
    char wintitle[128];
    if (cfg_connect_name[0]) {
      snprintf(wintitle, sizeof(wintitle), "CGTerm - %s [CONNECTED]", cfg_connect_name);
    } else {
      snprintf(wintitle, sizeof(wintitle), "CGTerm - %s:%d [CONNECTED]", host, port);
    }
    gfx_set_title(wintitle);
  }

  /* In ANSI mode the board is a Telnet server: offer terminal type and
   * window size and ask for suppress-go-ahead up front (what SyncTERM and
   * friends do), so boards that never ask still get the right size and
   * type. PETSCII boards are often raw TCP, where IAC bytes would print as
   * garbage, so nothing is sent unasked there. */
  if (cfg_termmode == 1) {
    unsigned char offer[12] = {0xFF, 0xFB, 0x18,   /* WILL TERMINAL-TYPE */
                               0xFF, 0xFB, 0x1F,   /* WILL NAWS */
                               0xFF, 0xFD, 0x03,   /* DO SUPPRESS-GO-AHEAD */
                               0xFF, 0xFB, 0x03};  /* WILL SUPPRESS-GO-AHEAD */
    net_write(offer, 12);
    tn_offered_ttype = tn_offered_naws = tn_offered_sga = 1;
  }
  return(0);
}


static signed int net_raw_byte(void) {
  if (tn_pushback >= 0) {
    int c = tn_pushback;
    tn_pushback = -1;
    return c;
  }
  if (!ISCONNECTED()) {
    return(-2);
  }
  if (!buflen) {
    buflen = recv(conn, (char *)buffer, BUFSIZE, 0);
    if (buflen == 0) {
      net_disconnect();
      return(-2);
    } else if (buflen < 0) {
      buflen = 0;
      if (WOULDBLOCK() || errno == EINTR) {
	return(-1);
      } else {
	if (net_status) net_status(2, (char *)net_errstr(net_lasterr()));
	net_disconnect();
	return(-2);
      }
    }
    bufptr = buffer;
  }
  --buflen;
  return(*bufptr++);
}


static signed int net_telnet_byte(void);

/* Byte receive for file transfer protocols. On a peer that never negotiated
 * Telnet every byte (including 0xFF) is passed through untouched. On a
 * Telnet peer the IAC layer is still applied, because such a server sends a
 * data byte 0xFF as IAC IAC and may inject option negotiation mid-stream;
 * passing those through raw corrupts Punter/XMODEM blocks (and is the
 * mirror image of net_send(), which doubles outgoing 0xFF on such links). */
signed int net_receive_raw(void) {
  if (net_telnet_seen) {
    return net_telnet_byte();
  }
  return net_raw_byte();
}


signed int net_receive(void) {
  return net_telnet_byte();
}


static signed int net_telnet_byte(void) {
  signed int c;
  int work = 0;

  for (;;) {
    /* Bound the work done per call. A peer that streams only Telnet option
     * negotiation (e.g. a flood of IAC DO <opt>) never produces a data byte,
     * so without this the loop spins across recv() refills and starves the
     * main SDL loop. Yield with "no data"; tn_state is static, so the next
     * call resumes mid-stream. */
    if (++work > 4096) {
      return -1;
    }
    switch (tn_state) {

    case TN_DATA:
      c = net_raw_byte();
      if (c < 0) return c;
      if (c != 0xFF) {
        if (tn_data_bytes < TN_DETECT_WINDOW) ++tn_data_bytes;
        return c;
      }
      tn_state = TN_IAC;
      break;

    case TN_IAC:
      c = net_raw_byte();
      if (c < 0) return c;    /* resume in TN_IAC on the next call */
      if (c == 0xFF) {
        tn_state = TN_DATA;
        if (!net_telnet_seen) {
          /* No negotiation seen yet: a raw C64 board sending two PETSCII
           * pi characters (0xFF) is far more likely than a Telnet escape. */
          tn_pushback = 0xFF;
        }
        return 0xFF;          /* escaped 0xFF literal */
      } else if ((c == 0xFB || c == 0xFC || c == 0xFD || c == 0xFE) && tn_may_start()) {
        net_telnet_seen = 1;   /* real negotiation: peer speaks Telnet */
        tn_cmd = c;            /* WILL / WONT / DO / DONT */
        tn_state = TN_OPT;
      } else if (c == 0xFA && tn_may_start()) {
        net_telnet_seen = 1;
        tn_state = TN_SB_OPT; /* subnegotiation */
      } else if (c >= 0xF0 && net_telnet_seen) {
        tn_state = TN_DATA;   /* two-byte command (NOP, GA, AYT...) — discard */
      } else {
        /* Not a Telnet command: a raw board sent PETSCII pi (0xFF) followed
         * by an ordinary byte. Deliver both instead of eating them. */
        tn_state = TN_DATA;
        tn_pushback = c;
        return 0xFF;
      }
      break;

    case TN_OPT:
      c = net_raw_byte();
      if (c < 0) return c;
      if (tn_cmd == 0xFD) {
        /* DO — server asks us to enable an option */
        unsigned char response[3] = {0xFF, 0xFC, (unsigned char)c}; /* WONT by default */
        int already = (c == 0x18 && tn_offered_ttype) || (c == 0x1F && tn_offered_naws) ||
                      (c == 0x03 && tn_offered_sga);
        if (c == 0x18 || c == 0x1F || c == 0x00 || c == 0x03) {
          /* Terminal Type / NAWS / Binary / SGA — we support these */
          response[1] = 0xFB; /* WILL */
        }
        if (!already && tn_replies < TN_MAX_REPLIES) {
          ++tn_replies;
          net_write(response, 3);   /* an answer to our own offer needs no echo */
        }
        if (c == 0x1F && !tn_naws_agreed) {
          /* Send window size now that NAWS was agreed (once: a peer
           * repeating DO NAWS must not get a 9-byte reply every time) */
          tn_naws_agreed = 1;
          net_send_naws_sb();
        }
      } else if (tn_cmd == 0xFB) {
        /* WILL — server offers an option, respond with DO or DONT */
        unsigned char response[3] = {0xFF, 0xFE, (unsigned char)c}; /* DONT by default */
        if (c == 0x01 || c == 0x03) {
          /* Echo or Suppress Go Ahead — accept */
          response[1] = 0xFD; /* DO */
        }
        if (!(c == 0x03 && tn_offered_sga) && tn_replies < TN_MAX_REPLIES) {
          ++tn_replies;
          net_write(response, 3);
        }
      }
      /* WONT(FC) and DONT(FE) — acknowledge silently */
      tn_state = TN_DATA;
      break;

    case TN_SB_OPT:
      c = net_raw_byte();
      if (c < 0) return c;
      tn_sb_opt = c;
      tn_sb_prev = 0;
      tn_sb_iter = 0;
      tn_sb_first = -1;
      tn_state = TN_SB_DATA;
      break;

    case TN_SB_DATA:
      /* Consume subnegotiation bytes until IAC SE (0xFF 0xF0). */
      c = net_raw_byte();
      if (c < 0) return c;
      if (tn_sb_prev == 0xFF && c == 0xF0) {
        /* Respond to Terminal Type request (option 24, SEND=1) */
        /* ...and only to an actual SEND, within the reply budget */
        if (tn_sb_opt == 0x18 && tn_sb_first == 0x01 && tn_replies < TN_MAX_REPLIES) {
          ++tn_replies;
          /* CBM identifies us as a C64 client in PETSCII mode; ANSI otherwise */
          const char *ttype = cfg_termmode == 1 ? "ANSI" : "CBM";
          unsigned char resp[64];
          int len = 0, ti;
          resp[len++] = 0xFF; /* IAC */
          resp[len++] = 0xFA; /* SB */
          resp[len++] = 0x18; /* Terminal Type */
          resp[len++] = 0x00; /* IS */
          for (ti = 0; ttype[ti] && len < 58; ti++)
            resp[len++] = ttype[ti];
          resp[len++] = 0xFF; /* IAC */
          resp[len++] = 0xF0; /* SE */
          net_write(resp, len);
        }
        tn_state = TN_DATA;
      } else if (tn_sb_prev == 0xFF && c == 0xFF) {
        tn_sb_prev = 0;   /* escaped 0xFF inside the subnegotiation data */
      } else {
        if (tn_sb_iter == 0) tn_sb_first = c;
        tn_sb_prev = c;
        if (++tn_sb_iter >= 4096) {
          /* Unterminated subnegotiation — treat as protocol abuse. */
          net_disconnect();
          return -2;
        }
      }
      break;

    default:
      tn_state = TN_DATA;
      break;
    }
  }
}


void net_send(unsigned char c) {
  if (ISCONNECTED()) {
    if (c == 0xFF && net_telnet_seen) {
      /* On a Telnet connection a literal 0xFF data byte must be doubled,
       * otherwise the server interprets it as IAC and corrupts the stream
       * (PETSCII pi, and 0xFF inside Punter/XMODEM upload blocks). */
      unsigned char esc[2] = {0xFF, 0xFF};
      net_write(esc, 2);
    } else {
      net_write(&c, 1);
    }
  }
}


void net_send_string(const unsigned char *s) {
  while (*s) {
    net_send(*s++);
  }
}


void net_disconnect(void) {
  if (ISCONNECTED()) {
    CLOSESOCKET(conn);
    conn = INVALID_SOCKET;
    buflen = 0;
    net_reset_telnet();
    if (net_session_reset_hook) net_session_reset_hook();
    cfg_connect_name[0] = 0;
    gfx_set_title("CGTerm [DISCONNECTED]");
  }
}


int net_connected(void) {
  return(ISCONNECTED());
}


#ifdef CGTERM_TEST
/* Test seam (tests/test_net.c, tests/fuzz_net.c): run the Telnet layer
 * over an already-connected descriptor such as one end of a socketpair,
 * as if net_connect() had just succeeded. Not compiled into the program. */
void net_test_attach(int fd) {
  conn = fd;
  buflen = 0;
  net_reset_telnet();
  tn_offered_ttype = tn_offered_naws = tn_offered_sga = 0;
  tn_naws_agreed = 0;
}
#endif
