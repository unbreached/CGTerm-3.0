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


int net_connect(const char *host, int port, void (*status)(int, char *)) {
  struct sockaddr_in address;
  struct hostent *hostent;
#if defined(__WIN32__) || defined(WINDOWS)
  WSADATA wsaData;
  u_long nonblock = 1;

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

  if (isdigit((int) host[strlen(host) - 1])) {
    address.sin_addr.s_addr = inet_addr(host);
  } else {
    hostent = gethostbyname(host); 
    if (hostent) { 
      address.sin_addr = *((struct in_addr *) hostent->h_addr);
    }
    else {
      if (net_status) net_status(2, "Unknown host");
      return(1);
    }
  }

  buflen = 0;
  bufptr = buffer;

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
          snprintf(wintitle, sizeof(wintitle), "CGTerm - %s:%d [CONNECTED]", host, port);
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
    c = net_raw_byte();
    if (c < 0) return c;

    if (c != 0xFF) return c;

    /* IAC detected */
    c = net_raw_byte();
    if (c < 0) return c;

    if (c == 0xFF) {
      /* Escaped 0xFF literal */
      return 0xFF;
    } else if (c == 0xFB || c == 0xFC || c == 0xFD || c == 0xFE) {
      /* WILL(FB) / WONT(FC) / DO(FD) / DONT(FE): consume the option byte */
      int cmd = c;
      c = net_raw_byte();
      if (c < 0) return c;
      /* Respond to Telnet negotiation */
      if (cmd == 0xFD) {
        /* DO — server asks us to enable an option */
        unsigned char response[3] = {0xFF, 0xFC, (unsigned char)c}; /* WONT by default */
        if (c == 0x18) {
          /* Terminal Type — we support this */
          response[1] = 0xFB; /* WILL */
        } else if (c == 0x1F) {
          /* NAWS (Negotiate About Window Size) — we support this */
          response[1] = 0xFB; /* WILL */
        } else if (c == 0x00) {
          /* Binary transmission */
          response[1] = 0xFB; /* WILL */
        }
        send(conn, (const char *)response, 3, 0);
        /* Send window size if NAWS was agreed */
        if (c == 0x1F) {
          unsigned char naws[9] = {0xFF, 0xFA, 0x1F,
            0, (unsigned char)cfg_columns, 0, (unsigned char)cfg_rows,
            0xFF, 0xF0};
          send(conn, (const char *)naws, 9, 0);
        }
      } else if (cmd == 0xFB) {
        /* WILL — server offers an option, respond with DO or DONT */
        unsigned char response[3] = {0xFF, 0xFE, (unsigned char)c}; /* DONT by default */
        if (c == 0x01 || c == 0x03) {
          /* Echo or Suppress Go Ahead — accept */
          response[1] = 0xFD; /* DO */
        }
        send(conn, (const char *)response, 3, 0);
      }
      /* WONT(FC) and DONT(FE) — just acknowledge silently */
    } else if (c == 0xFA) {
      /* SB: subnegotiation */
      int sb_opt = net_raw_byte();
      if (sb_opt < 0) return sb_opt;
      /* Consume until IAC SE (0xFF 0xF0) */
      {
        int prev = 0;
        for (;;) {
          c = net_raw_byte();
          if (c < 0) return c;
          if (prev == 0xFF && c == 0xF0) break;
          prev = c;
        }
      }
      /* Respond to Terminal Type request (option 24, SEND=1) */
      if (sb_opt == 0x18) {
        const char *ttype = cfg_termmode == 1 ? "ANSI" : "ANSI";
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
    }
    /* Otherwise discard the two bytes (IAC + command) and loop */
  }
}


void net_send(unsigned char c) {
  if (ISCONNECTED()) {
    send(conn, (const char *)&c, 1, 0);
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
    gfx_set_title("CGTerm [DISCONNECTED]");
  }
}


int net_connected(void) {
  return(ISCONNECTED());
}
