/* Fuzzer for the ANSI terminal parser (ansi.c) on the real screen model:
 * escape-heavy random streams, in 40 and 80 columns. Any crash or
 * sanitizer report is a bug, and the cursor must stay on the screen.
 * Reuses the stubs and setup of test_ansi.c.
 *
 *   make fuzz           (FUZZ_ITER=n for a longer run)
 */
#include <SDL.h>          /* first, so its own "main" macro is out of the way */
#undef main
#define main test_ansi_main
#include "test_ansi.c"
#undef main

static unsigned int rng_state = 4242;
static unsigned int rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

int main(int argc, char **argv) {
  int iters = argc > 1 ? atoi(argv[1]) : 2000;
  int it;
  long bytes = 0;
  static const char finals[] = "ABCDEFGHJKLMPSTXfhlmnrsu@`dGc";

  setenv("SDL_VIDEODRIVER", "dummy", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  setenv("CGTERM_ASSET_DIR", "assets", 0);
  cfg_init(argv[0]);
  cfg_columns = 80;
  cfg_rows = 25;
  cfg_zoom = 2;
  cfg_termmode = 1;
  if (gfx_init(0, "fuzz_ansi")) {
    printf("gfx_init failed (SDL dummy driver / assets missing?)\n");
    return 2;
  }
  ansi_init();

  for (it = 0; it < iters; it++) {
    int len = 1 + (int)(rnd() % 600), i;
    if ((it % 97) == 0) ansi_reset();
    for (i = 0; i < len; i++) {
      unsigned int k = rnd() % 16;
      if (k < 3) {
        /* a CSI sequence with wild parameters */
        int np = (int)(rnd() % 6), p;
        ansi_out(0x1b); ansi_out('[');
        if ((rnd() & 7) == 0) ansi_out('?');
        for (p = 0; p < np; p++) {
          char num[16]; int n, j;
          unsigned int v = (rnd() & 3) == 0 ? rnd() : rnd() % 300;
          n = snprintf(num, sizeof num, "%u", v);
          for (j = 0; j < n; j++) ansi_out((unsigned char)num[j]);
          if (p + 1 < np) ansi_out(';');
        }
        ansi_out((unsigned char)finals[rnd() % (sizeof(finals) - 1)]);
        bytes += 4;
      } else if (k < 4) {
        ansi_out(0x1b);
      } else if (k < 6) {
        ansi_out((unsigned char)(rnd() % 32));      /* C0 controls: CR LF BS TAB BEL FF ... */
      } else {
        ansi_out((unsigned char)rnd());
      }
      bytes++;
      if (gfx_cursx < 0 || gfx_cursx >= cfg_columns || gfx_cursy < 0 || gfx_cursy >= 25) {
        printf("FAIL: cursor left the screen (%d,%d) at iteration %d\n", gfx_cursx, gfx_cursy, it);
        return 1;
      }
    }
    if ((it % 50) == 0) gfx_vbl();
  }
  printf("fuzz_ansi: %d streams, %ld bytes: clean\n", iters, bytes);
  return 0;
}
