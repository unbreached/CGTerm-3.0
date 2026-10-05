/* Disk-image regression harness for the October 2026 fixes in diskimage.c,
 * plus the disk tools (validate / lock / rename disk / entry info). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskimage.h"
#include "dir.h"

extern unsigned char *get_ts_addr(DiskImage *, TrackSector);
extern int blocks_free(DiskImage *);
extern RawDirEntry *find_file_entry_exact(DiskImage *, const unsigned char *);

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

static void raw(unsigned char *r, const char *n) { memset(r, 0xa0, 16); memcpy(r, n, strlen(n)); }

static int write_file(DiskImage *di, const char *name, int bytes, FileType t) {
  unsigned char r[16]; ImageFile *f; unsigned char buf[4096]; int left = bytes, total = 0;
  raw(r, name);
  if ((f = di_open(di, r, t, "wb")) == NULL) return -1;
  memset(buf, 0x55, sizeof(buf));
  while (left > 0) { int n = left > 4096 ? 4096 : left; int w = di_write(f, buf, n); total += w; if (w != n) break; left -= n; }
  di_close(f);
  return total;
}

static int read_file(DiskImage *di, const char *name, FileType t) {
  unsigned char r[16]; ImageFile *f; unsigned char buf[4096]; int n, total = 0;
  raw(r, name);
  if ((f = di_open(di, r, t, "rb")) == NULL) return -1;
  while ((n = di_read(f, buf, 4096)) > 0) total += n;
  di_close(f);
  return total;
}

static DiskImage *fresh(const char *path, int size) {
  unsigned char name[16], id[2] = {'0', '1'};
  DiskImage *di = di_create_image((char *)path, size);
  raw(name, "TEST");
  di_format(di, name, id);
  return di;
}

int main(void) {
  DiskImage *di;
  unsigned char r[16], r2[16];
  TrackSector ts;

  puts("[D81 directory header + lookup]");
  di = fresh("/tmp/cgt-test.d81", 819200);
  ts.track = 40; ts.sector = 0;
  { unsigned char *hdr = get_ts_addr(di, ts);
    CHECK(hdr[0] == 40 && hdr[1] == 3 && hdr[2] == 'D' && hdr[3] == 0, "D81 header links 40/3 with DOS type 'D'"); }
  CHECK(write_file(di, "FILE1", 1000, T_PRG) == 1000, "write FILE1 on D81");
  CHECK(write_file(di, "FILE2", 1000, T_PRG) == 1000, "write FILE2 on D81");
  CHECK(read_file(di, "FILE1", T_PRG) == 1000, "read FILE1 back on D81");
  CHECK(write_file(di, "FILE1", 10, T_PRG) == -1 && di->status == 63, "duplicate name rejected on D81");
  raw(r, "FILE1");
  CHECK(di_delete(di, r, T_PRG) == 1, "delete FILE1 on D81");
  CHECK(di_track_blocks_free(di, 40) == 36, "track 40 keeps 36 free (no stray dir blocks)");
  CHECK(di->blocksfree == blocks_free(di), "blocksfree matches BAM after D81 ops");
  di_free_image(di);
  { Dir *d = dir_read("/tmp/cgt-test.d81"); int n = 0; DirEntry *e = d ? d->firstentry : NULL;
    while (e) { if (e->name && strcmp(e->name, "FILE2") == 0) n++; e = e->next; }
    CHECK(n == 1, "dir_read lists FILE2 on the synced D81"); if (d) dir_free(d); }

  puts("[D71 second side allocation]");
  di = fresh("/tmp/cgt-test.d71", 349696);
  CHECK(write_file(di, "BIG", 300000, T_PRG) == 300000, "300000-byte file fits on a D71");
  CHECK(di->blocksfree == blocks_free(di), "blocksfree matches BAM on D71");
  CHECK(di_track_blocks_free(di, 18) == 19 - 2 || di_track_blocks_free(di, 18) >= 17, "no file data on D71 directory track");
  { int before = di->blocksfree; int w = write_file(di, "FILL", 254 * 200, T_PRG);
    printf("  info disk-full write accepted %d of %d bytes with %d blocks free\n", w, 254 * 200, before);
    CHECK(w < 254 * 200, "disk-full write is short");
    raw(r, "FILL");
    CHECK(di_open(di, r, T_PRG, "rb") == NULL, "incomplete FILL entry was rolled back (not left as splat)");
    CHECK(blocks_free(di) == before, "rolled-back blocks are free again") ; }
  di_free_image(di);

  puts("[D64 allocation and chains]");
  di = fresh("/tmp/cgt-test.d64", 174848);
  { int i; for (i = 0; i < 200; i++) { char nm[16]; snprintf(nm, sizeof nm, "F%d", i); if (write_file(di, nm, 254 * 3, T_PRG) < 254 * 3) break; }
    CHECK(di_track_blocks_free(di, 18) >= 19 - 19 && di_track_blocks_free(di, 18) == 19 - (1 + (di_track_blocks_free(di,18) < 18 ? 19 - di_track_blocks_free(di,18) - 1 : 0)), "track 18 only holds directory blocks");
    CHECK(di->blocksfree == blocks_free(di), "blocksfree matches BAM after filling D64"); }
  di_free_image(di);

  di = fresh("/tmp/cgt-test.d64", 174848);
  CHECK(write_file(di, "EMPTY", 0, T_PRG) == 0, "write empty file");
  CHECK(read_file(di, "EMPTY", T_PRG) == 0, "empty file reads back 0 bytes (not block 1/0)");
  CHECK(write_file(di, "THREE", 254 * 3, T_PRG) == 254 * 3, "write 3-block file");
  /* corrupt THREE's second link to track 99 and read: must stop, not alias block 1/0 */
  { unsigned char *p; RawDirEntry *rde;
    raw(r, "THREE"); rde = find_file_entry_exact(di, r);
    p = get_ts_addr(di, rde->startts); p = get_ts_addr(di, *(TrackSector *)p); p[0] = 99; p[1] = 0;
    CHECK(read_file(di, "THREE", T_PRG) == 508 && di->status == 66, "bad link ends the file with status 66");
    /* free twice via a looped chain must not wrap the count */
    { int tb = di_track_blocks_free(di, rde->startts.track);
      di_free_ts(di, rde->startts); di_free_ts(di, rde->startts);
      CHECK(di_track_blocks_free(di, rde->startts.track) == tb + 1, "freeing a free block does not double count"); } }
  /* exact-name delete with a wildcard character in the name */
  CHECK(write_file(di, "A*", 10, T_PRG) == 10 && write_file(di, "ABC", 10, T_PRG) == 10, "write A* and ABC");
  raw(r, "A*"); raw(r2, "ABC");
  CHECK(di_delete_exact(di, r) == 1, "delete A* exactly");
  CHECK(read_file(di, "ABC", T_PRG) == 10, "ABC survived the wildcard-looking delete");
  CHECK(di_rename_exact(di, r2, r2) == 63, "rename onto an existing name is refused (63)");
  di_free_image(di);

  puts("[error-info image sizes]");
  { FILE *f = fopen("/tmp/cgt-test.d64", "ab"); unsigned char z[683]; memset(z, 1, sizeof z); fwrite(z, 1, sizeof z, f); fclose(f);
    di = di_load_image("/tmp/cgt-test.d64");
    CHECK(di != NULL && di->size == 174848 + 683, "175531-byte D64 (with error bytes) loads");
    if (di) { CHECK(read_file(di, "ABC", T_PRG) == 10, "file readable in error-byte image");
      write_file(di, "NEW", 100, T_PRG); di_free_image(di);
      { FILE *g = fopen("/tmp/cgt-test.d64", "rb"); long sz; fseek(g, 0, SEEK_END); sz = ftell(g); fclose(g);
        CHECK(sz == 174848 + 683, "error table preserved on write-back"); } } }

  puts("[validate rebuilds the BAM]");
  { int fixed = 0, removed = 0, expect_free, i; unsigned char *bam;
    di = fresh("/tmp/cgt-test.d64", 174848);
    CHECK(write_file(di, "KEEP1", 254 * 4, T_PRG) == 254 * 4, "write KEEP1 (4 blocks)");
    CHECK(write_file(di, "KEEP2", 100, T_SEQ) == 100, "write KEEP2 (1 block)");
    CHECK(write_file(di, "SPLAT", 254 * 2, T_PRG) == 254 * 2, "write SPLAT (2 blocks)");
    expect_free = di->blocksfree + 2;   /* SPLAT's two blocks come back */
    /* turn SPLAT into an unclosed entry, as an interrupted save leaves it */
    raw(r, "SPLAT"); find_file_entry_exact(di, r)->type &= ~0x80;
    /* wreck the BAM: zero the whole allocation map */
    ts.track = 18; ts.sector = 0; bam = get_ts_addr(di, ts); memset(bam + 4, 0, 35 * 4);
    CHECK(blocks_free(di) == 0, "zeroed BAM reports 0 blocks free");
    CHECK(di_validate(di, &fixed, &removed) == 0, "di_validate returns 0");
    CHECK(removed == 1, "one splat entry removed");
    CHECK(di->blocksfree == expect_free && blocks_free(di) == expect_free, "free count rebuilt from the directory chains");
    CHECK(fixed == expect_free, "fixed_blocks = freed - before (before was 0)");
    CHECK(di_track_blocks_free(di, 18) == 17, "18/0 and 18/1 re-allocated, rest of track 18 free");
    raw(r, "SPLAT");
    CHECK(find_file_entry_exact(di, r) == NULL, "splat entry gone from the directory");
    CHECK(read_file(di, "KEEP1", T_PRG) == 254 * 4 && read_file(di, "KEEP2", T_SEQ) == 100, "closed files still readable after validate");
    /* every block of KEEP1 is allocated again */
    { RawDirEntry *rde; raw(r, "KEEP1"); rde = find_file_entry_exact(di, r); ts = rde->startts;
      for (i = 0; i < 4 && ts.track; i++) { if (di_is_ts_free(di, ts)) break; ts = *(TrackSector *)get_ts_addr(di, ts); }
      CHECK(i == 4, "all 4 KEEP1 blocks are marked used"); }
    /* a looped chain must terminate: point KEEP1's last block back at its first */
    { RawDirEntry *rde; unsigned char *q; raw(r, "KEEP1"); rde = find_file_entry_exact(di, r); ts = rde->startts;
      for (i = 0; i < 3; i++) ts = *(TrackSector *)get_ts_addr(di, ts);
      q = get_ts_addr(di, ts); q[0] = rde->startts.track; q[1] = rde->startts.sector;
      CHECK(di_validate(di, &fixed, &removed) == 0 && removed == 0 && blocks_free(di) == expect_free, "cyclic chain terminates and keeps the count"); }
    /* all-0xFF BAM (everything free, counts nonsense) is also rebuilt */
    memset(bam + 4, 0xff, 35 * 4);
    CHECK(di_validate(di, &fixed, &removed) == 0 && blocks_free(di) == expect_free, "all-0xFF BAM rebuilt");
    CHECK(fixed < 0, "fixed_blocks negative when the bad BAM over-reported free space");
    di_free_image(di);
    di = di_load_image("/tmp/cgt-test.d64");
    CHECK(di && di->blocksfree == expect_free, "validated BAM persisted through write-back");
    di_free_image(di); }

  { int fixed, removed;
    di = fresh("/tmp/cgt-test.d81", 819200);
    CHECK(write_file(di, "ONE", 254 * 50, T_PRG) == 254 * 50, "D81: write 50-block file");
    { int before = di->blocksfree; unsigned char *b1 = get_ts_addr(di, di->bam), *b2 = get_ts_addr(di, di->bam2);
      memset(b1 + 16, 0, 240); memset(b2 + 16, 0, 240);
      CHECK(di_validate(di, &fixed, &removed) == 0 && blocks_free(di) == before && removed == 0, "D81 BAM (both sectors) rebuilt to the same free count");
      CHECK(!di_is_ts_free(di, di->bam) && !di_is_ts_free(di, di->bam2) && !di_is_ts_free(di, di->dir), "D81 header + both BAM sectors allocated");
      CHECK(read_file(di, "ONE", T_PRG) == 254 * 50, "D81 file intact after validate"); }
    di_free_image(di);
    di = fresh("/tmp/cgt-test.d71", 349696);
    CHECK(write_file(di, "BIG", 254 * 800, T_PRG) == 254 * 800, "D71: 800-block file spans both sides");
    { int before = di->blocksfree; unsigned char *b = get_ts_addr(di, di->bam);
      memset(b + 4, 0x5a, 35 * 4); memset(b + 0xdd, 0x5a, 35); memset(get_ts_addr(di, di->bam2), 0x5a, 105);
      CHECK(di_validate(di, &fixed, &removed) == 0 && blocks_free(di) == before, "D71 BAM (18/0 counts + 53/0 bitmaps) rebuilt");
      CHECK(di_track_blocks_free(di, 53) == 0, "D71 track 53 stays reserved");
      CHECK(read_file(di, "BIG", T_PRG) == 254 * 800, "D71 file intact after validate"); }
    di_free_image(di); }

  puts("[lock toggle persists]");
  di = fresh("/tmp/cgt-test.d64", 174848);
  CHECK(write_file(di, "LOCKME", 300, T_PRG) == 300, "write LOCKME");
  raw(r, "LOCKME");
  { int t = -1, b = -1, c = -1, l = -1;
    CHECK(di_set_locked(di, r, 1) == 0, "lock LOCKME");
    CHECK(di_entry_info(di, r, &t, &b, &c, &l) == 0 && t == T_PRG && b == 2 && c == 1 && l == 1, "info: PRG, 2 blocks, closed, locked");
    raw(r2, "NOPE");
    CHECK(di_set_locked(di, r2, 1) == 62 && di_entry_info(di, r2, NULL, NULL, NULL, NULL) == 62, "lock/info of a missing file -> 62"); }
  di_free_image(di);
  di = di_load_image("/tmp/cgt-test.d64");
  { int l = 0; di_entry_info(di, r, NULL, NULL, NULL, &l); CHECK(l == 1, "lock bit survived write-back"); }
  CHECK(di_delete_exact(di, r) == 1, "a locked file can still be scratched by the exact delete");
  CHECK(di_set_locked(di, r, 0) == 62, "...and is gone");
  CHECK(write_file(di, "LOCKME", 300, T_PRG) == 300 && di_set_locked(di, r, 1) == 0 && di_set_locked(di, r, 0) == 0, "lock then unlock");
  { int l = 1; di_entry_info(di, r, NULL, NULL, NULL, &l); CHECK(l == 0, "unlocked again"); }
  { Dir *d; DirEntry *e; int found = 0; di_free_image(di); d = dir_read("/tmp/cgt-test.d64");
    for (e = d ? d->firstentry : NULL; e; e = e->next) if (e->name && strcmp(e->name, "LOCKME") == 0) { found = 1; CHECK(e->locked == 0 && e->closed && e->size == 2, "dir_read sees the entry unlocked, closed, 2 blocks"); }
    CHECK(found, "dir_read lists LOCKME"); if (d) dir_free(d); }

  puts("[rename disk roundtrip]");
  { unsigned char nm[16], id[2] = {'Z', '9'}; Dir *d;
    di = fresh("/tmp/cgt-test.d64", 174848);
    raw(nm, "RENAMED DISK");
    CHECK(di_rename_disk(di, nm, id) == 0, "D64 rename disk");
    CHECK(memcmp(di_title(di), nm, 16) == 0 && memcmp(di_id(di), "Z9", 2) == 0 && memcmp(di_dostype(di), "2A", 2) == 0, "D64 header name/ID/DOS type");
    di_free_image(di);
    di = di_load_image("/tmp/cgt-test.d64");
    CHECK(di && memcmp(di_title(di), nm, 16) == 0 && memcmp(di_id(di), "Z9", 2) == 0, "D64 rename persisted");
    CHECK(di_rename_disk(di, NULL, (const unsigned char *)"Q1") == 0 && memcmp(di_title(di), nm, 16) == 0 && memcmp(di_id(di), "Q1", 2) == 0, "ID-only change keeps the name");
    di_free_image(di);
    d = dir_read("/tmp/cgt-test.d64");
    CHECK(d && d->title && strcmp(d->title, "RENAMED DISK") == 0 && strcmp((char *)d->id, "Q1") == 0 && strcmp((char *)d->dostype, "2A") == 0, "dir_read exposes title/id/dostype on D64");
    if (d) dir_free(d);
    d = dir_read("/tmp");
    CHECK(d && d->id[0] == 0 && d->dostype[0] == 0 && d->blocksfree == -1, "folder Dir has empty id/dostype");
    if (d) dir_free(d);

    di = fresh("/tmp/cgt-test.d81", 819200);
    raw(nm, "BIG DISK");
    CHECK(di_rename_disk(di, nm, id) == 0 && memcmp(di_title(di), nm, 16) == 0 && memcmp(di_id(di), "Z9", 2) == 0 && memcmp(di_dostype(di), "3D", 2) == 0, "D81 header name/ID/DOS type");
    CHECK(memcmp(get_ts_addr(di, di->bam) + 4, "Z9", 2) == 0 && memcmp(get_ts_addr(di, di->bam2) + 4, "Z9", 2) == 0, "D81 BAM sectors carry the new ID");
    CHECK(write_file(di, "AFTER", 10, T_PRG) == 10 && read_file(di, "AFTER", T_PRG) == 10, "D81 still usable after rename");
    di_free_image(di);
    d = dir_read("/tmp/cgt-test.d81");
    CHECK(d && d->title && strcmp(d->title, "BIG DISK") == 0 && strcmp((char *)d->id, "Z9") == 0 && strcmp((char *)d->dostype, "3D") == 0, "dir_read exposes title/id/dostype on D81");
    if (d) dir_free(d);
    di = fresh("/tmp/cgt-test.d71", 349696);
    CHECK(di_rename_disk(di, nm, id) == 0 && memcmp(di_title(di), nm, 16) == 0 && memcmp(di_id(di), "Z9", 2) == 0, "D71 rename disk");
    di_free_image(di); }

  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
