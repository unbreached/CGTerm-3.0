/* Telnet layer of net.c, driven over a socketpair.
 * Built with -DCGTERM_TEST, which adds net_test_attach() to net.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include "net.h"

/* ---- what net.c needs from the rest of the program ---- */
int cfg_termmode = 0;          /* 0 = PETSCII, 1 = ANSI */
int cfg_columns = 40, cfg_rows = 25;
char cfg_connect_name[128] = "";
void cfg_log_connection(const char *h, int p) { (void)h; (void)p; }
void gfx_set_title(const char *t) { (void)t; }
void gfx_vbl(void) {}
void ansi_reset(void) {}

void net_test_attach(int fd);

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

static int peer = -1;

static void fresh(int termmode) {
  int sv[2];
  if (peer >= 0) { net_disconnect(); close(peer); }
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); exit(2); }
  fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL, 0) | O_NONBLOCK);
  cfg_termmode = termmode;
  net_test_attach(sv[0]);
  peer = sv[1];
}
static void put(const void *p, int n) { if (write(peer, p, (size_t)n) != n) { perror("write"); exit(2); } }
/* everything the client delivers as data until the socket is drained */
static int take(unsigned char *out, int max, signed int (*rx)(void)) {
  int n = 0, idle = 0;
  while (n < max && idle < 3) {
    signed int c = rx();
    if (c == -2) break;
    if (c < 0) { idle++; continue; }
    idle = 0;
    out[n++] = (unsigned char)c;
  }
  return n;
}
/* everything the client wrote back */
static int sent(unsigned char *out, int max) {
  int n = (int)read(peer, out, (size_t)max);
  return n < 0 ? 0 : n;
}
static int count3(const unsigned char *b, int n, int b0, int b1, int b2) {
  int i, c = 0;
  for (i = 0; i + 2 < n; i++) if (b[i] == b0 && b[i + 1] == b1 && b[i + 2] == b2) c++;
  return c;
}

int main(void) {
  unsigned char in[70000], out[70000];
  int n, m, i;

  puts("[Telnet: a real server negotiating at connect]");
  fresh(1);
  put("\xff\xfb\x01\xff\xfb\x03\xff\xfd\x18\xff\xfd\x1fHi", 14);   /* WILL ECHO, WILL SGA, DO TTYPE, DO NAWS */
  n = take(in, sizeof in, net_receive);
  m = sent(out, sizeof out);
  CHECK(n == 2 && in[0] == 'H' && in[1] == 'i', "negotiation is consumed, only the text is delivered");
  CHECK(count3(out, m, 0xff, 0xfd, 0x01) == 1, "WILL ECHO answered with DO ECHO");
  CHECK(count3(out, m, 0xff, 0xfb, 0x18) == 1, "DO TTYPE answered with WILL TTYPE");
  CHECK(count3(out, m, 0xff, 0xfa, 0x1f) == 1, "window size sent once after DO NAWS");
  put("\xff\xfa\x18\x01\xff\xf0", 6);                                /* SB TTYPE SEND */
  n = take(in, sizeof in, net_receive);
  m = sent(out, sizeof out);
  CHECK(n == 0 && m >= 10 && memcmp(out, "\xff\xfa\x18\x00" "ANSI\xff\xf0", 10) == 0, "TTYPE SEND answered with IS ANSI");
  put("\xff\xfa\x18\x00" "xx\xff\xf0", 8);                          /* SB TTYPE IS: not a request */
  n = take(in, sizeof in, net_receive);
  m = sent(out, sizeof out);
  CHECK(n == 0 && m == 0, "a subnegotiation that is not SEND gets no reply");

  puts("[Telnet: 0xFF in data on a Telnet link]");
  put("A\xff\xff" "B", 4);
  n = take(in, sizeof in, net_receive_raw);
  CHECK(n == 3 && in[0] == 'A' && in[1] == 0xff && in[2] == 'B', "incoming IAC IAC is one 0xFF (transfer path too)");
  net_send(0xff); net_send('Z');
  m = sent(out, sizeof out);
  CHECK(m == 3 && out[0] == 0xff && out[1] == 0xff && out[2] == 'Z', "outgoing 0xFF is doubled");

  puts("[Raw PETSCII board: pi and graphics must not look like Telnet]");
  fresh(0);
  memset(in, 'x', 600); put(in, 600);                                /* past the detection window */
  n = take(in, sizeof in, net_receive);
  CHECK(n == 600, "600 plain bytes delivered");
  put("\xff\xfb\x01" "Q\xff\xfa\xff\xfd\x18", 9);                    /* pi + graphics bytes in art */
  n = take(in, sizeof in, net_receive);
  m = sent(out, sizeof out);
  CHECK(n == 9 && memcmp(in, "\xff\xfb\x01" "Q\xff\xfa\xff\xfd\x18", 9) == 0, "bytes that look like IAC WILL/SB/DO pass through as data");
  CHECK(m == 0, "and nothing is sent back");
  net_send(0xff);
  m = sent(out, sizeof out);
  CHECK(m == 1 && out[0] == 0xff, "outgoing 0xFF is not doubled on a raw board");
  put("\xff\xff", 2);
  n = take(in, sizeof in, net_receive_raw);
  CHECK(n == 2 && in[0] == 0xff && in[1] == 0xff, "two pi characters arrive as two bytes");

  puts("[PETSCII board that really is behind Telnet]");
  fresh(0);
  put("\xff\xfd\x18" "ok", 5);                                      /* negotiation in the first bytes */
  n = take(in, sizeof in, net_receive);
  m = sent(out, sizeof out);
  CHECK(n == 2 && count3(out, m, 0xff, 0xfb, 0x18) == 1, "negotiation at connect is honoured in PETSCII mode");
  put("\xff\xfa\x18\x01\xff\xf0", 6);
  take(in, sizeof in, net_receive);
  m = sent(out, sizeof out);
  CHECK(m >= 9 && memcmp(out, "\xff\xfa\x18\x00" "CBM\xff\xf0", 9) == 0, "terminal type is CBM in PETSCII mode");

  puts("[Abuse]");
  fresh(1);
  /* DO <unknown> x 2000, as one write: thousands of 3-byte writes run a
   * Linux socketpair out of buffer space long before 6000 bytes */
  for (i = 0; i < 2000; i++) memcpy(in + i * 3, "\xff\xfd\x55", 3);
  put(in, 6000);
  n = 0; for (i = 0; i < 20; i++) n += take(in, sizeof in, net_receive);
  m = 0; { int r; while ((r = sent(out + m, (int)sizeof out - m)) > 0) m += r; }
  CHECK(n == 0, "an option flood delivers no data");
  CHECK(m > 0 && m <= 256 * 3, "replies to an option flood are capped at 256");
  fresh(1);
  put("\xff\xfa\x18", 3);
  memset(in, 'j', 5000); put(in, 5000);                              /* subnegotiation that never ends */
  n = 0; for (i = 0; i < 20 && net_connected(); i++) n += take(in, sizeof in, net_receive);
  CHECK(!net_connected(), "an endless subnegotiation drops the connection");
  fresh(1);
  put("\xff", 1);                                                     /* IAC split across two reads */
  n = take(in, sizeof in, net_receive);
  put("\xfb\x01" "x", 3);
  n += take(in + n, (int)sizeof in - n, net_receive);
  CHECK(n == 1 && in[0] == 'x', "a command split across reads is still parsed");

  net_disconnect(); close(peer);
  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
