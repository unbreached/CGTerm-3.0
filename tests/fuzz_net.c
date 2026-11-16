/* Fuzzer for the Telnet layer of net.c: random and Telnet-shaped byte
 * streams are pushed through a socketpair, in both terminal modes, and
 * everything the client does with them must stay within bounds. Any crash
 * or sanitizer report is a bug. Built with -DCGTERM_TEST.
 *
 *   make fuzz           (FUZZ_ITER=n for a longer run)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include "net.h"

int cfg_termmode = 0;
int cfg_columns = 40, cfg_rows = 25;
char cfg_connect_name[128] = "";
void cfg_log_connection(const char *h, int p) { (void)h; (void)p; }
void gfx_set_title(const char *t) { (void)t; }
void gfx_vbl(void) {}
void ansi_reset(void) {}
void net_test_attach(int fd);

static unsigned int rng_state = 20261116;
static unsigned int rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

int main(int argc, char **argv) {
  int iters = argc > 1 ? atoi(argv[1]) : 2000;
  int it, worst_reply = 0;
  long total_in = 0, total_data = 0;

  for (it = 0; it < iters; it++) {
    unsigned char in[2048], junk[8192];
    int sv[2], len, i, replied = 0, r, spins;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); return 2; }
    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL, 0) | O_NONBLOCK);
    cfg_termmode = (int)(rnd() & 1);
    net_test_attach(sv[0]);

    /* a stream that is mostly Telnet commands, with plain and 0xFF data mixed in */
    len = 1 + (int)(rnd() % (sizeof(in) - 1));
    for (i = 0; i < len; i++) {
      unsigned int k = rnd() % 10;
      if (k < 4) in[i] = 0xff;
      else if (k < 7) in[i] = (unsigned char)(0xf0 + rnd() % 16);
      else if (k < 8) in[i] = (unsigned char)(rnd() % 40);       /* option numbers, SEND/IS */
      else in[i] = (unsigned char)rnd();
    }
    if (write(sv[1], in, (size_t)len) != len) { perror("write"); return 2; }
    total_in += len;

    /* drain through both receive paths, sending now and then, until idle */
    for (spins = 0; spins < 64 && net_connected(); spins++) {
      signed int c = (rnd() & 1) ? net_receive() : net_receive_raw();
      if (c >= 0) { total_data++; spins = 0; }
      if ((rnd() & 31) == 0) net_send((unsigned char)rnd());
      if ((rnd() & 255) == 0) net_send_naws();
      while ((r = (int)read(sv[1], junk, sizeof junk)) > 0) replied += r;
    }
    while ((r = (int)read(sv[1], junk, sizeof junk)) > 0) replied += r;
    if (replied > worst_reply) worst_reply = replied;

    /* 256 capped replies of at most 64 bytes, plus what the loop itself sent */
    if (replied > 256 * 64 + 4096) {
      printf("FAIL: %d reply bytes for %d input bytes (iteration %d)\n", replied, len, it);
      return 1;
    }
    net_disconnect();
    close(sv[0]);
    close(sv[1]);
  }
  printf("fuzz_net: %d streams, %ld bytes in, %ld delivered as data, largest reply %d bytes: clean\n",
         iters, total_in, total_data, worst_reply);
  return 0;
}
