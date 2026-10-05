/* Loopback harness: scripted peer + virtual clock for xmodem.c / punter.c / xfer.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
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

  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
