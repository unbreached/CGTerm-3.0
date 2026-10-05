/* Loopback harness: scripted peer + virtual clock for xmodem.c / punter.c / xfer.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <sys/stat.h>
#include "xfer.h"
#include "xmodem.h"
#include "punter.h"
#include "crc.h"
#include "keyboard.h"

/* ---- stubs for the UI / config / net / timer the transfer code touches ---- */
int cfg_debugmode = 0;
int cfg_transferlog = 0;
char cfg_connect_name[128] = "";
char *cfg_host = NULL;
void cfg_user_file(char *out, size_t sz, const char *name) { snprintf(out, sz, "/tmp/%s", name); }
char cfg_dldir[256] = "/tmp";
char cfg_xferdir[256] = "/tmp";
Focus kbd_focus;
void menu_update_xfer_progress(const char *m, int a, int b) { (void)m; (void)a; (void)b; }
void menu_update_xfer_block_progress(const char *m, const char *p, int a, int b) { (void)m; (void)p; (void)a; (void)b; }
void menu_xfer_feed_byte(unsigned char c) { (void)c; }
void menu_draw_message(const char *m) { printf("    [msg] %s\n", m); }
void menu_draw_message_timed(const char *m, int t) { (void)t; printf("    [msg] %s\n", m); }
void menu_draw_xfer_progress(const char *f, int d, int p) { (void)f; (void)d; (void)p; }
void menu_show(void) {}
void gfx_vbl(void) {}
void ui_inputcall(int w, char *t, char *x, void (*cb)(char *), Focus f) { (void)w; (void)t; (void)x; (void)cb; (void)f; }
void ui_inputcall_on_cancel(void (*cb)(void)) { (void)cb; }

static unsigned int vclock = 0;
unsigned int timer_get_ticks(void) { return vclock; }
void timer_delay(unsigned int ms) { vclock += ms; }

/* scripted peer */
static unsigned char inq[1 << 20]; static int inq_head = 0, inq_tail = 0;
static unsigned char outq[1 << 20]; static int outq_len = 0;
static int disconnected = 0;
static void feed(const unsigned char *p, int n) { memcpy(inq + inq_tail, p, n); inq_tail += n; }
static void feedb(int c) { unsigned char b = (unsigned char)c; feed(&b, 1); }
static void (*peer_on_byte)(unsigned char) = NULL;
signed int net_receive_raw(void) {
  if (disconnected) return -2;
  if (inq_head < inq_tail) return inq[inq_head++];
  return -1;
}
void net_send(unsigned char c) { outq[outq_len++] = c; if (peer_on_byte) peer_on_byte(c); }
int net_connected(void) { return !disconnected; }
static void peer_reset(void) { inq_head = inq_tail = 0; outq_len = 0; disconnected = 0; vclock = 0; peer_on_byte = NULL; }

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

/* ---------------- XMODEM scripted sender ---------------- */
static unsigned char xfile[4096]; static int xfile_len;
static int xs_block, xs_crc, xs_corrupt_block, xs_dup_block, xs_ignore_C, xs_eot_sent;
static void xs_send_block(int blk) {
  unsigned char b[135]; int i, off = (blk - 1) * 128; unsigned char ck = 0;
  if (off >= xfile_len) { feedb(0x04); xs_eot_sent = 1; return; }
  b[0] = 0x01; b[1] = blk & 0xff; b[2] = 0xff - (blk & 0xff);
  for (i = 0; i < 128; i++) b[3 + i] = off + i < xfile_len ? xfile[off + i] : 0x1a;
  if (xs_crc) { unsigned short c = crc16_calc(b + 3, 128); b[131] = c >> 8; b[132] = c & 0xff; }
  else { for (i = 0; i < 128; i++) ck += b[3 + i]; b[131] = ck; }
  if (xs_corrupt_block == blk) { b[10] ^= 0x55; xs_corrupt_block = 0; }   /* corrupt AFTER the check bytes */
  feed(b, xs_crc ? 133 : 132);
}
static void xs_peer(unsigned char c) {
  if (c == 'C' && xs_block == 0) { if (xs_ignore_C) { feedb('C'); return; } xs_crc = 1; xs_block = 1; xs_send_block(1); }
  else if (c == 0x15 && xs_block == 0) { xs_crc = 0; xs_block = 1; xs_send_block(1); }
  else if (c == 0x15 && xs_block > 0) { xs_send_block(xs_block); }
  else if (c == 0x06 && xs_block > 0 && !xs_eot_sent) {
    if (xs_dup_block == xs_block) { xs_dup_block = 0; xs_send_block(xs_block); return; }  /* lost ACK: resend */
    xs_block++; xs_send_block(xs_block);
  }
}
static int tempfile_matches(const unsigned char *data, int len) {
  extern char xfer_tempdlname[]; FILE *f = fopen(xfer_tempdlname, "rb"); unsigned char buf[8192]; int n;
  if (!f) return 0; n = (int)fread(buf, 1, sizeof buf, f); fclose(f);
  return n == len && memcmp(buf, data, len) == 0;
}
static void run_xmodem_recv(const char *label, int usecrc, int ignoreC, int corrupt, int dup, int lead_garbage) {
  int rc, i;
  peer_reset();
  xfile_len = 300; for (i = 0; i < xfile_len; i++) xfile[i] = (unsigned char)(i * 7 + 3);
  xfile[150] = 0x1a; xfile[299] = 0x1a;   /* real 0x1a bytes inside the data and at the end */
  xs_block = 0; xs_corrupt_block = corrupt; xs_dup_block = dup; xs_ignore_C = ignoreC; xs_eot_sent = 0; xs_crc = 0;
  if (lead_garbage) feed((const unsigned char *)"\r\nStart your download now\r\n", 27);
  peer_on_byte = xs_peer;
  xfer_protocol = usecrc ? PROT_XMODEMCRC : PROT_XMODEM; xfer_direction = DIR_RECV;
  rc = xfer_recv();
  printf("  [%s] rc=%d saved=%d vclock=%us\n", label, rc, xfer_saved_bytes, vclock / 1000);
  /* XMODEM pads to 128: trailing 0x1a of the last block is trimmed, so a file
   * that genuinely ends in 0x1a loses it (inherent to the protocol). */
  CHECK(rc == 1 && xfer_saved_bytes == 299 && tempfile_matches(xfile, 299), label);
}

/* ---------------- Punter scripted sender (BBS side) ----------------
 * Response table driven by what punter_recv() sends:
 *   GOO -> ACK (always)      SYN -> S/B      BAD -> ACK (and resend)
 *   S/B #1 -> filetype block, #2 -> SYN, #3 -> 7-byte header, then data
 *   blocks in order; after the last data block S/B -> SYN. */
static unsigned char pfile[4096]; static int pfile_len;
static int ps_sb, ps_blk, ps_resend_once, ps_last_sent, ps_done;
static char ps_acc[4];
static void ps_build(unsigned char *buf, int datalen, int blknum, int nextsize) {
  unsigned short ck = 0, clc = 0; int i; int len = datalen + 7;
  buf[4] = (unsigned char)nextsize; buf[5] = blknum & 0xff; buf[6] = (blknum >> 8) & 0xff;
  for (i = 4; i < len; i++) { ck += buf[i]; clc ^= buf[i]; clc = (clc << 1) | (clc >> 15); }
  buf[0] = ck & 0xff; buf[1] = ck >> 8; buf[2] = clc & 0xff; buf[3] = clc >> 8;
}
static int ps_datalen(int blk) { int off = (blk - 1) * 247, left = pfile_len - off; return left > 247 ? 247 : (left > 0 ? left : 0); }
static void ps_send_data_block(int blk) {
  unsigned char buf[260]; int dl = ps_datalen(blk), ndl = ps_datalen(blk + 1);
  memcpy(buf + 7, pfile + (blk - 1) * 247, dl);
  if (ndl == 0) { ps_build(buf, dl, 0xffff, 0); ps_done = 1; } else ps_build(buf, dl, blk, ndl + 7);
  feed(buf, dl + 7);
  ps_last_sent = blk;
}
static void ps_peer(unsigned char c) {
  unsigned char buf[16];
  ps_acc[0] = ps_acc[1]; ps_acc[1] = ps_acc[2]; ps_acc[2] = (char)c; ps_acc[3] = 0;
  if (strcmp(ps_acc, "GOO") == 0 || strcmp(ps_acc, "BAD") == 0) { feed((const unsigned char *)"ACK", 3); memset(ps_acc, 0, 4); return; }
  if (strcmp(ps_acc, "SYN") == 0) { feed((const unsigned char *)"S/B", 3); memset(ps_acc, 0, 4); return; }
  if (strcmp(ps_acc, "S/B") != 0) return;
  memset(ps_acc, 0, 4);
  ps_sb++;
  if (ps_sb == 1) { memset(buf, 0, 8); buf[7] = 2; ps_build(buf, 1, 0xffff, 0); feed(buf, 8); return; }
  if (ps_sb == 2) { feed((const unsigned char *)"SYN", 3); return; }
  if (ps_sb == 3) { memset(buf, 0, 7); ps_build(buf, 0, 0, ps_datalen(1) + 7); feed(buf, 7); ps_blk = 1; return; }
  if (ps_done) { feed((const unsigned char *)"SYN", 3); return; }
  if (ps_resend_once && ps_last_sent == 1) { ps_resend_once = 0; ps_send_data_block(1); return; }  /* sender missed our GOO: same block again */
  ps_send_data_block(ps_blk++);
}
static void run_punter_recv(const char *label, int len, int resend) {
  int rc, i;
  peer_reset();
  pfile_len = len; for (i = 0; i < len; i++) pfile[i] = (unsigned char)(i * 13 + 1);
  ps_sb = 0; ps_blk = 0; ps_resend_once = resend; ps_last_sent = 0; ps_done = 0; memset(ps_acc, 0, 4);
  peer_on_byte = ps_peer;
  xfer_protocol = PROT_PUNTER; xfer_direction = DIR_RECV;
  rc = xfer_recv();
  printf("  [%s] rc=%d saved=%d vclock=%us sb=%d\n", label, rc, xfer_saved_bytes, vclock / 1000, ps_sb);
  CHECK(rc == 1 && xfer_saved_bytes == len && tempfile_matches(pfile, len), label);
}

/* ---------------- XMODEM send to a scripted receiver ---------------- */
static int xr_state, xr_expect, xr_got_eot; static unsigned char xr_data[8192]; static int xr_datalen;
static void xr_peer(unsigned char c) {
  static unsigned char blk[1030]; static int n = 0;
  if (xr_got_eot) return;
  if (n == 0 && c == 0x04) { xr_got_eot = 1; feedb(0x06); return; }
  blk[n++] = c;
  if (n == 133) { /* SOH blk ~blk 128 crc2 */
    unsigned short crc = crc16_calc(blk + 3, 128), rc = (blk[131] << 8) | blk[132];
    if (blk[0] == 1 && blk[1] == (xr_expect & 0xff) && crc == rc) { memcpy(xr_data + xr_datalen, blk + 3, 128); xr_datalen += 128; xr_expect++; feedb(0x06); }
    else feedb(0x15);
    n = 0;
  }
}

/* ---------------- Multi Punter upload: scripted C*Base receiver ----------------
 * Mirrors bbs.bas: the board waits for 0x09 + name + CR (get#5 loop), then
 * runs initrecv2 (8-byte filetype block, $ffff -> SYN exchange) and
 * receive2 (7-byte header, data blocks, $ffff last block -> SYN exchange),
 * then returns to the get#5 loop. 0x09 0x04 ends the batch. */
#include "diskimage.h"
#include "dir.h"
#include "fileselector.h"
static unsigned char mp_rx[3][8192]; static int mp_rx_len[3]; static char mp_names[3][64]; static int mp_files;
static int mp_batch_end, mp_filetypes[3];
static int mp_state, mp_blklen, mp_blkpos, mp_nextsize, mp_badchecksum_once, mp_bad_done;
static unsigned char mp_blk[300];
static char mp_acc[4];
static int mp_namepos;
static int mp_checksum_ok(const unsigned char *b, int len) {
  unsigned short ck = 0, clc = 0; int i;
  for (i = 4; i < len; i++) { ck += b[i]; clc ^= b[i]; clc = (clc << 1) | (clc >> 15); }
  return ck == (b[0] | (b[1] << 8)) && clc == (b[2] | (b[3] << 8));
}
static void mp_send(const char *s) { feed((const unsigned char *)s, 3); }
/* states: 0 wait TAB, 1 reading name, 2 wait ACK(after GOO) phase1, 3 wait S/B? no: we send S/B and read 8 bytes (state 3 = reading block),
 * 4 wait ACK after GOO (block accepted), 5 wait SYN, 6 wait S/B (end of SYN exchange), 7 wait ACK (phase 2 GOO),
 * 8 reading header/data block, 9 wait ACK after GOO for data block, 10 wait SYN (final), 11 wait S/B (final) */
static int mp_phase;   /* 1 = filetype block, 2 = header, 3 = data */
static void mp_expect_block(int len) { mp_blklen = len; mp_blkpos = 0; mp_state = 8; mp_send("S/B"); }
static void mp_peer(unsigned char c) {
  if (mp_state == 0) {
    if (c == 0x09) { mp_state = 1; mp_namepos = 0; }
    return;
  }
  if (mp_state == 1) {
    if (c == 0x04 && mp_namepos == 0) { mp_batch_end = 1; mp_state = 0; return; }
    if (c == 0x09) return;
    if (c == '\r' || c == '\n') {
      mp_names[mp_files][mp_namepos] = 0;
      mp_rx_len[mp_files] = 0;
      mp_phase = 1;
      mp_state = 2;
      mp_send("GOO");          /* initrecv2 -> recvblk: GOO, wait ACK */
      return;
    }
    if (mp_namepos < 63) mp_names[mp_files][mp_namepos++] = (char)c;
    return;
  }
  if (mp_state == 8) {
    mp_blk[mp_blkpos++] = c;
    if (mp_blkpos < mp_blklen) return;
    /* whole block in */
    if (!mp_checksum_ok(mp_blk, mp_blklen) || (mp_badchecksum_once && !mp_bad_done && mp_phase == 3)) {
      mp_bad_done = 1;
      mp_state = 12;            /* BAD, wait ACK, then S/B again */
      mp_send("BAD");
      return;
    }
    if (mp_phase == 1) { mp_filetypes[mp_files] = mp_blk[7]; }
    if (mp_phase == 3 && mp_blklen > 7) {
      memcpy(mp_rx[mp_files] + mp_rx_len[mp_files], mp_blk + 7, mp_blklen - 7);
      mp_rx_len[mp_files] += mp_blklen - 7;
    }
    mp_nextsize = mp_blk[4];
    mp_state = 9;
    mp_send("GOO");
    return;
  }
  mp_acc[0] = mp_acc[1]; mp_acc[1] = mp_acc[2]; mp_acc[2] = (char)c; mp_acc[3] = 0;
  switch (mp_state) {
  case 2:  /* ACK to our GOO (phase 1 or 2 entry) */
    if (strcmp(mp_acc, "ACK") == 0) { memset(mp_acc, 0, 4); mp_expect_block(mp_phase == 1 ? 8 : 7); mp_phase = (mp_phase == 1) ? 1 : 2; }
    break;
  case 12: /* ACK to our BAD */
    if (strcmp(mp_acc, "ACK") == 0) { memset(mp_acc, 0, 4); mp_expect_block(mp_blklen); }
    break;
  case 9:  /* ACK to our GOO after a good block */
    if (strcmp(mp_acc, "ACK") != 0) break;
    memset(mp_acc, 0, 4);
    if (mp_phase == 1 || (mp_phase >= 2 && mp_blk[5] == 0xff && mp_blk[6] == 0xff)) {
      /* recvblk sets lastblkflag on ANY $ffff block, the 7-byte header included */
      /* lastblkflag: S/B, wait SYN, SYN, wait S/B */
      mp_state = 10; mp_send("S/B");
    } else if (mp_phase == 2) {
      mp_phase = 3; mp_expect_block(mp_nextsize);
    } else {
      mp_expect_block(mp_nextsize);
    }
    break;
  case 10:
    if (strcmp(mp_acc, "SYN") == 0) { memset(mp_acc, 0, 4); mp_state = 11; mp_send("SYN"); }
    break;
  case 11:
    if (strcmp(mp_acc, "S/B") == 0) {
      memset(mp_acc, 0, 4);
      if (mp_phase == 1) { mp_phase = 2; mp_state = 2; mp_send("GOO"); }   /* receive2 -> recvblk */
      else { mp_files++; mp_state = 0; }                                    /* back to get#5 */
    }
    break;
  default: break;
  }
}
static void run_multipunter_send(const char *label, int nfiles, const int *sizes, int badonce) {
  static DirEntry ents[3]; static Dir dir; static FileSelector fs;
  static unsigned char data[3][8192];
  char path[64]; int i, ok = 1;
  peer_reset();
  memset(&mp_rx_len, 0, sizeof(mp_rx_len)); mp_files = 0; mp_batch_end = 0; mp_state = 0; mp_bad_done = 0; mp_badchecksum_once = badonce;
  memset(mp_acc, 0, 4);
  snprintf(cfg_xferdir, 256, "/tmp/cgt-mp");
  mkdir("/tmp/cgt-mp", 0755);
  memset(&dir, 0, sizeof(dir)); memset(ents, 0, sizeof(ents)); memset(&fs, 0, sizeof(fs));
  for (i = 0; i < nfiles; i++) {
    int k; FILE *f;
    for (k = 0; k < sizes[i]; k++) data[i][k] = (unsigned char)(k * 31 + i * 7 + 1);
    snprintf(path, sizeof(path), "/tmp/cgt-mp/file%d.prg", i);
    f = fopen(path, "wb"); fwrite(data[i], 1, sizes[i], f); fclose(f);
    ents[i].name = malloc(32); snprintf(ents[i].name, 32, "file%d.prg", i);
    ents[i].type = T_PRG; ents[i].tagged = 1; ents[i].size = sizes[i];
    if (i) ents[i - 1].next = &ents[i];
  }
  dir.firstentry = &ents[0]; dir.numentries = nfiles; dir.blocksfree = -1;
  fs.dir = &dir; fs.numtagged = nfiles; fs.selectedfile = &ents[0];
  peer_on_byte = mp_peer;
  xfer_send_multipunter(&fs);
  printf("  [%s] files=%d batch_end=%d vclock=%us\n", label, mp_files, mp_batch_end, vclock / 1000);
  if (mp_files != nfiles || !mp_batch_end) ok = 0;
  for (i = 0; i < nfiles && ok; i++) {
    char want[64]; snprintf(want, sizeof(want), "FILE%d,p", i);
    if (mp_rx_len[i] != sizes[i] || memcmp(mp_rx[i], data[i], sizes[i]) != 0) { printf("    file %d: got %d bytes, want %d\n", i, mp_rx_len[i], sizes[i]); ok = 0; }
    if (strcmp(mp_names[i], want) != 0) { printf("    file %d: announced as '%s', want '%s'\n", i, mp_names[i], want); ok = 0; }
    if (mp_filetypes[i] != 2) { printf("    file %d: filetype %d, want 2\n", i, mp_filetypes[i]); ok = 0; }
  }
  CHECK(ok, label);
  for (i = 0; i < nfiles; i++) free(ents[i].name);
}

int main(int argc, char **argv) { (void)argc; (void)argv;
  int rc, i;
  crc_init();
  puts("[XMODEM receive]");
  run_xmodem_recv("CRC, clean", 1, 0, 0, 0, 0);
  run_xmodem_recv("CRC, leading prompt text + echoed C", 1, 0, 0, 0, 1);
  run_xmodem_recv("CRC, corrupted block 2 then resend", 1, 0, 2, 0, 0);
  run_xmodem_recv("CRC, duplicate block 2 (lost ACK)", 1, 0, 0, 2, 0);
  run_xmodem_recv("checksum-only sender: falls back from C to NAK", 1, 1, 0, 0, 0);
  run_xmodem_recv("checksum mode", 0, 0, 0, 0, 0);

  puts("[XMODEM receive: disconnect / junk]");
  peer_reset(); xfer_protocol = PROT_XMODEMCRC; xfer_direction = DIR_RECV; disconnected = 1;
  rc = xfer_recv(); CHECK(rc == 0 && vclock < 20000, "disconnected peer fails fast");
  peer_reset(); xfer_protocol = PROT_XMODEMCRC; xfer_direction = DIR_RECV;
  for (i = 0; i < 5000; i++) feedb('x');
  rc = xfer_recv(); CHECK(rc == 0 && vclock < 200000, "endless junk stream aborts (no infinite loop)");

  puts("[XMODEM send]");
  peer_reset(); xfile_len = 300; for (i = 0; i < 300; i++) xfile[i] = (unsigned char)(i * 3 + 1);
  { FILE *f = fopen("/tmp/cgt-up.bin", "wb"); fwrite(xfile, 1, 300, f); fclose(f); }
  xr_state = 0; xr_expect = 1; xr_got_eot = 0; xr_datalen = 0; peer_on_byte = xr_peer; feedb('C');
  xfer_protocol = PROT_XMODEMCRC; xfer_direction = DIR_SEND;
  rc = xfer_send("cgt-up.bin", NULL);
  CHECK(rc == 1 && xr_got_eot && xr_datalen == 384 && memcmp(xr_data, xfile, 300) == 0, "CRC send delivers the file and EOT");
  peer_reset(); disconnected = 1; xfer_protocol = PROT_XMODEMCRC; xfer_direction = DIR_SEND;
  rc = xfer_send("cgt-up.bin", NULL); CHECK(rc == 0 && vclock < 20000, "send to a dead socket fails fast");
  peer_reset(); xfer_protocol = PROT_XMODEMCRC; xfer_direction = DIR_SEND;
  rc = xfer_send("cgt-up.bin", NULL); CHECK(rc == 0 && vclock >= 60000 && vclock < 120000, "silent receiver times out after ~60 s");

  puts("[Punter receive]");
  run_punter_recv("247*2 bytes", 494, 0);
  run_punter_recv("248 bytes: last block carries 1 data byte", 248, 0);
  run_punter_recv("1 byte file", 1, 0);
  run_punter_recv("block 1 retransmitted (lost GOO)", 500, 1);

  puts("[Multi Punter upload to a C*Base-style receiver]");
  { int sz[3] = {494, 248, 1}; run_multipunter_send("3 files (494, 248, 1 bytes)", 3, sz, 0); }
  { int sz[1] = {3000}; run_multipunter_send("1 file, one block rejected with BAD then resent", 1, sz, 1); }
  { int sz[2] = {0, 100}; run_multipunter_send("empty file followed by a normal one", 2, sz, 0); }

  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
