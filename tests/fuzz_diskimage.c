/* Mutation fuzzer for the disk-image parser: formats an image, writes a
 * few files, then flips random bytes in the directory/BAM area and runs
 * every parsing entry point. Any crash or sanitizer report is a bug.
 *
 * Build and run (optionally with -fsanitize=address,undefined):
 *   make fuzz           (default 2000 iterations; FUZZ_ITER=n to change)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskimage.h"
#include "dir.h"

static void raw(unsigned char *r, const char *n) { memset(r, 0xa0, 16); memcpy(r, n, strlen(n)); }

static unsigned int rng_state = 12345;
static unsigned int rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static void exercise(const char *path) {
  DiskImage *di = di_load_image((char *)path);
  Dir *d;
  unsigned char buf[4096];
  int fixed, removed;

  if (!di) return;
  {
    unsigned char r[16];
    ImageFile *f;
    const char *names[] = {"ONE", "TWO", "THREE", "A*", "SPLAT", "$"};
    int i;
    for (i = 0; i < 6; i++) {
      raw(r, names[i]);
      if ((f = di_open(di, r, T_PRG, "rb")) != NULL) {
        while (di_read(f, buf, sizeof(buf)) > 0) { }
        di_close(f);
      }
      if ((f = di_open_exact(di, r)) != NULL) {
        while (di_read(f, buf, sizeof(buf)) > 0) { }
        di_close(f);
      }
    }
    raw(r, "FUZZNEW");
    if ((f = di_open(di, r, T_PRG, "wb")) != NULL) {
      memset(buf, 0x42, sizeof(buf));
      di_write(f, buf, 1000);
      di_close(f);
    }
    raw(r, "TWO");
    di_delete_exact(di, r);
    di_validate(di, &fixed, &removed);
  }
  di->modified = 0;   /* never write the mutated image back */
  di_free_image(di);

  d = dir_read(path);
  if (d) dir_free(d);
}

int main(int argc, char **argv) {
  const char *path = "/tmp/cgt-fuzz.d64";
  int iters = 2000, it, size = 174848;
  unsigned char *base, *work;
  FILE *f;

  if (argc > 1) iters = atoi(argv[1]);
  if (argc > 2 && strcmp(argv[2], "d81") == 0) { size = 819200; path = "/tmp/cgt-fuzz.d81"; }
  if (argc > 2 && strcmp(argv[2], "d71") == 0) { size = 349696; path = "/tmp/cgt-fuzz.d71"; }

  /* base image: formatted with a few files */
  {
    DiskImage *di = di_create_image((char *)path, size);
    unsigned char name[16], id[2] = {'F', 'Z'}, r[16], buf[1024];
    ImageFile *fh;
    int i;
    raw(name, "FUZZ");
    di_format(di, name, id);
    memset(buf, 0x11, sizeof(buf));
    for (i = 0; i < 3; i++) {
      const char *n[] = {"ONE", "TWO", "THREE"};
      raw(r, n[i]);
      if ((fh = di_open(di, r, T_PRG, "wb")) != NULL) { di_write(fh, buf, 254 * (i + 1) + 7); di_close(fh); }
    }
    di_free_image(di);
  }
  f = fopen(path, "rb");
  base = malloc(size + 4096);
  work = malloc(size + 4096);
  if (!f || !base || !work || (int)fread(base, 1, size, f) != size) { puts("setup failed"); return 1; }
  fclose(f);

  for (it = 0; it < iters; it++) {
    int flips = 1 + (int)(rnd() % 24), k, len = size;
    memcpy(work, base, size);
    for (k = 0; k < flips; k++) {
      /* bias towards the directory track and the first file blocks */
      unsigned int where = (rnd() % 4 == 0) ? (rnd() % size) : (size == 174848 ? 0x16500 + rnd() % 0x1300 : (size == 819200 ? 0x61800 + rnd() % 0x1000 : 0x16500 + rnd() % 0x1300));
      if (where >= (unsigned int)size) where %= size;
      work[where] = (unsigned char)rnd();
    }
    if (rnd() % 50 == 0) len = size - (int)(rnd() % 600);         /* truncated image */
    if (rnd() % 50 == 0) len = size + (size == 174848 ? 683 : 0);  /* error-byte image */
    f = fopen(path, "wb");
    fwrite(work, 1, len, f);
    fclose(f);
    exercise(path);
    if ((it + 1) % 500 == 0) { printf("  %d iterations ok\n", it + 1); fflush(stdout); }
  }
  printf("fuzz: %d iterations, no crash\n", iters);
  free(base);
  free(work);
  return 0;
}
