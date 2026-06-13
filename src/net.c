#include <sys/types.h>
#if defined(__WIN32__) || defined(WINDOWS)
# include <winsock.h>
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
#include <SDL.h>
#include "net.h"
#include "config.h"
#include "gfx.h"
#include "ansi.h"

#if defined(__WIN32__) || defined(WINDOWS)
/* Windows */
typedef int socklen_t;
#define WOULDBLOCK() (WSAGetLastError() == WSAEWOULDBLOCK)
#define CLOSESOCKET(s)  closesocket(s)
#else
/* Unix */
typedef int SOCKET;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#define WOULDBLOCK() (errno == EWOULDBLOCK)
#define CLOSESOCKET(s)  close(s)
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

/* Persistent Telnet IAC parser state, so an IAC sequence split across
 * recv() boundaries resumes correctly on the next net_receive() call
 * instead of desynchronising and leaking the command byte as data. */
enum { TN_DATA = 0, TN_IAC, TN_OPT, TN_SB_OPT, TN_SB_DATA };
static int tn_state = TN_DATA;
static int tn_cmd = 0;          /* WILL/WONT/DO/DONT while in TN_OPT */
static int tn_sb_opt = 0;       /* subnegotiation option while in TN_SB_* */
static int tn_sb_prev = 0;      /* previous byte, for detecting IAC SE */
static int tn_sb_iter = 0;      /* subnegotiation byte counter (abuse cap) */
static int net_telnet_seen = 0; /* peer speaks Telnet -> escape outgoing 0xFF */

static void net_reset_telnet(void) {
  tn_state = TN_DATA;
  tn_cmd = tn_sb_opt = tn_sb_prev = tn_sb_iter = 0;
  net_telnet_seen = 0;
}


int net_connect(const char *host, int port, void (*status)(int, char *)) {
  struct sockaddr_in address;
#ifdef WINDOWS
  struct hostent *hostent;
#endif
#if defined(__WIN32__) || defined(WINDOWS)
  WSADATA wsaData;
  u_long nonblock = 1;
#endif

  /* Reject NULL/empty host before any string indexing. */
  if (host == NULL || host[0] == 0) {
    if (status) status(2, "No host specified");
    return(1);
  }
#if defined(__WIN32__) || defined(WINDOWS)
  if (!winsockstarted) {
    if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
      printf("WinSock startup failed\n");
      return(1);
    }
    atexit((void (*)(void))WSACleanup);
    winsockstarted = 1;
  }
#endif
  net_status = status;
  if ((conn = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
    if (net_status) net_status(2, "Socket call failed");
    return(1);
  }
  address.sin_family = AF_INET;
  address.sin_port = htons(port);

  /* Treat host as a literal IPv4 address if it parses as one; otherwise
   * resolve it as a name. Don't decide from the trailing character — real
   * hostnames can legitimately end in a digit (e.g. "c64node1"). */
  address.sin_addr.s_addr = inet_addr(host);
  if (address.sin_addr.s_addr == INADDR_NONE) {
    /* Hostname resolution with Windows compatibility */
#ifdef WINDOWS
    /* Use traditional gethostbyname on Windows for MinGW compatibility */
    hostent = gethostbyname(host);
    if (hostent) {
      address.sin_addr = *((struct in_addr *) hostent->h_addr);
    } else {
      if (net_status) net_status(2, "Unknown host");
      CLOSESOCKET(conn);
      conn = INVALID_SOCKET;
      return(1);
    }
#else
    /* Use secure getaddrinfo() on modern systems */
    struct addrinfo hints, *result;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;        /* IPv4 only for compatibility */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;   /* Only return addresses if we have connectivity */

    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
      if (net_status) net_status(2, "Hostname resolution failed");
      CLOSESOCKET(conn);
      conn = INVALID_SOCKET;
      return(1);
    }

    if (result == NULL) {
      if (net_status) net_status(2, "No addresses found");
      CLOSESOCKET(conn);
      conn = INVALID_SOCKET;
      return(1);
    }

    /* Copy the first IPv4 address */
    address.sin_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr;
    freeaddrinfo(result);
#endif
  }

  buflen = 0;
  bufptr = buffer;
  net_reset_telnet();
  ansi_reset();   /* fresh escape-parser state for the new session */

  if (net_status) net_status(0, "Connecting...");

  /* Non-blocking connect with select() — allows ESC to cancel and
   * keeps the UI responsive during connection attempts */
  {
#if defined(__WIN32__) || defined(WIN32) || defined(WINDOWS)
    u_long nb = 1;
    ioctlsocket(conn, FIONBIO, &nb);
#else
    fcntl(conn, F_SETFL, O_NONBLOCK);
#endif

    connect(conn, (struct sockaddr *)&address, sizeof(address));

    /* Poll with select() — 10 second timeout, check ESC every 200ms */
    {
      fd_set wfds;
      struct timeval tv;
      int elapsed = 0;
      int connected = 0;

      while (elapsed < 10000) {
        SDL_Event ev;

        FD_ZERO(&wfds);
        FD_SET(conn, &wfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  /* 200ms */

        if (select((int)conn + 1, NULL, &wfds, NULL, &tv) > 0) {
          /* Check if connect succeeded */
          int err = 0;
          socklen_t errlen = sizeof(err);
          getsockopt(conn, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen);
          if (err == 0) {
            connected = 1;
            break;
          } else {
            /* Connect failed */
            break;
          }
        }

        elapsed += 200;

        /* Pump SDL events — check for ESC to cancel */
        while (SDL_PollEvent(&ev)) {
          if (ev.type == SDL_QUIT) exit(0);
          if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            CLOSESOCKET(conn);
            conn = INVALID_SOCKET;
            if (net_status) net_status(2, "Cancelled");
            return(1);
          }
        }

        /* Update screen so it doesn't look frozen */
        gfx_vbl();
      }

      if (connected) {
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
        return(0);
      } else {
        CLOSESOCKET(conn);
        conn = INVALID_SOCKET;
        if (net_status) net_status(2, "Connection timed out");
        return(1);
      }
    }
  }
}


static signed int net_raw_byte(void) {
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
      if (WOULDBLOCK()) {
	return(-1);
      } else {
	if (net_status) net_status(2, strerror(errno));
	net_disconnect();
	return(-2);
      }
    }
    bufptr = buffer;
  }
  --buflen;
  return(*bufptr++);
}


/* Raw byte receive — no Telnet IAC filtering.
 * Used by file transfer protocols that need all bytes including 0xFF. */
signed int net_receive_raw(void) {
  return net_raw_byte();
}


signed int net_receive(void) {
  signed int c;

  for (;;) {
    switch (tn_state) {

    case TN_DATA:
      c = net_raw_byte();
      if (c < 0) return c;
      if (c != 0xFF) return c;
      net_telnet_seen = 1;   /* peer speaks Telnet */
      tn_state = TN_IAC;
      break;

    case TN_IAC:
      c = net_raw_byte();
      if (c < 0) return c;    /* resume in TN_IAC on the next call */
      if (c == 0xFF) {
        tn_state = TN_DATA;
        return 0xFF;          /* escaped 0xFF literal */
      } else if (c == 0xFB || c == 0xFC || c == 0xFD || c == 0xFE) {
        tn_cmd = c;            /* WILL / WONT / DO / DONT */
        tn_state = TN_OPT;
      } else if (c == 0xFA) {
        tn_state = TN_SB_OPT; /* subnegotiation */
      } else {
        tn_state = TN_DATA;   /* two-byte command with no option — discard */
      }
      break;

    case TN_OPT:
      c = net_raw_byte();
      if (c < 0) return c;
      if (tn_cmd == 0xFD) {
        /* DO — server asks us to enable an option */
        unsigned char response[3] = {0xFF, 0xFC, (unsigned char)c}; /* WONT by default */
        if (c == 0x18 || c == 0x1F || c == 0x00) {
          /* Terminal Type / NAWS / Binary — we support these */
          response[1] = 0xFB; /* WILL */
        }
        send(conn, (const char *)response, 3, 0);
        if (c == 0x1F) {
          /* Send window size now that NAWS was agreed */
          unsigned char naws[9] = {0xFF, 0xFA, 0x1F,
            0, (unsigned char)cfg_columns, 0, (unsigned char)cfg_rows,
            0xFF, 0xF0};
          send(conn, (const char *)naws, 9, 0);
        }
      } else if (tn_cmd == 0xFB) {
        /* WILL — server offers an option, respond with DO or DONT */
        unsigned char response[3] = {0xFF, 0xFE, (unsigned char)c}; /* DONT by default */
        if (c == 0x01 || c == 0x03) {
          /* Echo or Suppress Go Ahead — accept */
          response[1] = 0xFD; /* DO */
        }
        send(conn, (const char *)response, 3, 0);
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
      tn_state = TN_SB_DATA;
      break;

    case TN_SB_DATA:
      /* Consume subnegotiation bytes until IAC SE (0xFF 0xF0). */
      c = net_raw_byte();
      if (c < 0) return c;
      if (tn_sb_prev == 0xFF && c == 0xF0) {
        /* Respond to Terminal Type request (option 24, SEND=1) */
        if (tn_sb_opt == 0x18) {
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
          send(conn, (const char *)resp, len, 0);
        }
        tn_state = TN_DATA;
      } else {
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
      send(conn, (const char *)esc, 2, 0);
    } else {
      send(conn, (const char *)&c, 1, 0);
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
    cfg_connect_name[0] = 0;
    gfx_set_title("CGTerm [DISCONNECTED]");
  }
}


int net_connected(void) {
  return(ISCONNECTED());
}
