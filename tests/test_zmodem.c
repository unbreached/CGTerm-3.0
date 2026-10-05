/* ZMODEM test harness for src/zmodem.c.
 *
 * Three kinds of peer, all behind the same net_receive_raw/net_send stubs:
 *   - real lrzsz children (sz / rz) spawned over pipes, for interop;
 *   - a fork of this program running our own sender against our receiver;
 *   - a scripted byte queue with a purely virtual clock, for the timeout,
 *     cancel and disconnect paths (no real waiting).
 * While a child is alive timer_delay() also sleeps 1 ms of real time so the
 * child gets scheduled; timer_get_ticks() always reports the virtual clock.
 *
 * Build (macOS/Homebrew; SDL is not actually needed by these objects):
 *   cc -O1 -g -Wall -Isrc -I/opt/homebrew/include/SDL -D_THREAD_SAFE -o /tmp/test_zmodem \
 *      tests/test_zmodem.c src/zmodem.c src/crc.c -L/opt/homebrew/lib -lSDLmain -lSDL -Wl,-framework,Cocoa
 * or simply:
 *   cc -O1 -g -Wall -Isrc -o /tmp/test_zmodem tests/test_zmodem.c src/zmodem.c src/crc.c
 * Needs sz and rz in PATH (brew install lrzsz). Run: /tmp/test_zmodem
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "xfer.h"
#include "crc.h"
#include "zmodem.h"

/* ---- xfer.c globals and byte layer (same semantics as xfer.c) ---------- */
Direction xfer_direction;
Protocol xfer_protocol;
char xfer_filename[256];
int xfer_cancel;
int xfer_saved_bytes;
int xfer_file_size;
unsigned char xfer_buffer[XFER_BUFFER_SIZE];
FILE *xfer_sendfile;

static char last_status[256];
void xfer_progress_status(const char *message, int current, int total) {
  (void)current; (void)total;
  snprintf(last_status, sizeof(last_status), "%s", message);
}

/* ---- virtual clock ----------------------------------------------------- */
static unsigned int vclock = 0;
static int real_time = 0;       /* a child process is alive: really sleep */
unsigned int timer_get_ticks(void) { return vclock; }
void timer_delay(unsigned int ms) { vclock += ms; if (real_time) usleep(1000); }

/* ---- peer: child process over pipes, or a scripted queue --------------- */
static pid_t peer_pid = -1;
static int peer_in = -1;        /* our bytes go here (child's stdin) */
static int peer_out = -1;       /* child's stdout */
static unsigned char inbuf[65536]; static int in_head = 0, in_tail = 0;
static int disconnected = 0;
static long rx_total = 0, tx_total = 0;
static long kill_peer_at_rx = -1;   /* kill the child after this many bytes received */
static long corrupt_rx_at = -1;     /* flip a bit in the Nth received byte */
static long corrupt_tx_at = -1;     /* flip a bit in the Nth sent byte */
static long cancel_at_rx = -1;      /* set xfer_cancel after N received bytes */

/* scripted mode (no child) */
static unsigned char inq[1 << 16]; static int inq_head = 0, inq_tail = 0;
static unsigned char outq[1 << 20]; static int outq_len = 0;

static FILE *tracefp = NULL;
static int trace_n = 0;
static void peer_reset(void) {
  if (getenv("ZM_TRACE")) {
    char tp[1100];
    if (tracefp) fclose(tracefp);
    snprintf(tp, sizeof(tp), "%s.%d", getenv("ZM_TRACE"), trace_n++);
    tracefp = fopen(tp, "w");
  }
  in_head = in_tail = 0; inq_head = inq_tail = 0; outq_len = 0;
  disconnected = 0; rx_total = tx_total = 0;
  kill_peer_at_rx = corrupt_rx_at = corrupt_tx_at = cancel_at_rx = -1;
  vclock = 0; xfer_cancel = 0; real_time = 0; last_status[0] = 0;
}

static void peer_close_fds(void) {
  if (peer_in >= 0) { close(peer_in); peer_in = -1; }
  if (peer_out >= 0) { close(peer_out); peer_out = -1; }
}

/* ZM_TRACE=<prefix>: log every byte of each test ("<" from peer, ">" to peer) */
static void trace(char dir, int c) {
  static char lastdir = 0;
  if (!tracefp) return;
  if (dir != lastdir) { fprintf(tracefp, "\n%c ", dir); lastdir = dir; }
  fprintf(tracefp, "%02x ", c & 0xff);
}

static int peer_spawn(const char *dir, char *const argv[]) {
  int p_in[2], p_out[2];

  if (pipe(p_in) || pipe(p_out)) return 0;
  peer_pid = fork();
  if (peer_pid < 0) return 0;
  if (peer_pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    dup2(p_in[0], 0); dup2(p_out[1], 1);
    if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
    close(p_in[0]); close(p_in[1]); close(p_out[0]); close(p_out[1]);
    if (dir && chdir(dir)) _exit(126);
    execvp(argv[0], argv);
    _exit(127);
  }
  close(p_in[0]); close(p_out[1]);
  peer_in = p_in[1]; peer_out = p_out[0];
  fcntl(peer_out, F_SETFL, O_NONBLOCK);
  real_time = 1;
  return 1;
}

/* returns the child's exit status (-1 if killed) */
static int peer_wait(void) {
  int st = 0;
  peer_close_fds();
  if (peer_pid > 0) waitpid(peer_pid, &st, 0);
  peer_pid = -1; real_time = 0;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

signed int net_receive_raw(void) {
  int c;
  if (disconnected) return -2;
  if (peer_out < 0) {                      /* scripted mode */
    if (inq_head < inq_tail) return inq[inq_head++];
    return -1;
  }
  if (in_head >= in_tail) {
    ssize_t n = read(peer_out, inbuf, sizeof(inbuf));
    if (n == 0) { disconnected = 1; return -2; }
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR) return -1;
      disconnected = 1; return -2;
    }
    in_head = 0; in_tail = (int)n;
  }
  c = inbuf[in_head++];
  rx_total++;
  if (corrupt_rx_at == rx_total) c ^= 0x55;
  trace('<', c);
  if (cancel_at_rx >= 0 && rx_total >= cancel_at_rx) xfer_cancel = 1;
  if (kill_peer_at_rx >= 0 && rx_total >= kill_peer_at_rx && peer_pid > 0) {
    kill(peer_pid, SIGKILL);
    in_head = in_tail = 0;                 /* drop what was already buffered */
    peer_wait();
    disconnected = 1;
    return -2;
  }
  return c;
}

void net_send(unsigned char c) {
  tx_total++;
  if (corrupt_tx_at == tx_total) c ^= 0x55;
  trace('>', c);
  if (peer_in < 0) { if (outq_len < (int)sizeof(outq)) outq[outq_len++] = c; return; }
  if (write(peer_in, &c, 1) != 1) disconnected = 1;
}

void xfer_send_byte(unsigned char c) { net_send(c); }

signed int xfer_recv_byte(int timeout) {
  signed int c;
  unsigned int starttime = timer_get_ticks();
  while ((c = net_receive_raw()) == -1) {
    if (timer_get_ticks() > starttime + (unsigned int)timeout) return -1;
    timer_delay(1);
  }
  return c;
}

/* ---- download file hooks (xfer_begin_file & co) ------------------------ */
static char recvdir[1024], recvtmp[1100], last_saved[1100];
static FILE *recvfp = NULL;
static int begin_count, end_count, abort_count;

int xfer_begin_file(void) {
  snprintf(recvtmp, sizeof(recvtmp), "%s/.partial", recvdir);
  recvfp = fopen(recvtmp, "wb+");
  xfer_saved_bytes = 0;
  begin_count++;
  return recvfp != NULL;
}
int xfer_end_file(const char *remote_name) {
  if (!recvfp) return 0;
  fclose(recvfp); recvfp = NULL;
  if (strchr(remote_name, '/') || strstr(remote_name, "..")) return 0;
  snprintf(last_saved, sizeof(last_saved), "%s/%s", recvdir, remote_name);
  end_count++;
  return rename(recvtmp, last_saved) == 0;
}
void xfer_abort_file(void) {
  if (recvfp) { fclose(recvfp); recvfp = NULL; }
  remove(recvtmp);
  abort_count++;
}
int xfer_save_data(unsigned char *data, int length) {
  if (!recvfp || (int)fwrite(data, 1, length, recvfp) != length) return 0;
  xfer_saved_bytes += length;
  return length;
}
int xfer_load_data(unsigned char *data, int length) {
  int l, got = 0;
  while (got < length) {
    l = (int)fread(data + got, 1, length - got, xfer_sendfile);
    if (l <= 0) break;
    got += l;
  }
  return got;
}

/* ---- test files -------------------------------------------------------- */
static char srcdir[1024], rzdir[1024];
static unsigned char *filedata[3]; static int filelen[3] = { 300, 100 * 1024, 0 };
static const char *filename[3] = { "f300.bin", "f100k.bin", "f0.bin" };

static void make_data(unsigned char *buf, int len, unsigned int seed) {
  /* sequences the ZDLE escaping must get right, then pseudo-random filler */
  static const unsigned char tricky[] = {
    '@', '\r', 0x18, 0x11, 0x13, 0x10, 0x90, 0x91, 0x93, 0x7f, 0xff, '*', '*', 0x18, 'B',
    '\r', '\n', 0x8d, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, '@', 0x8d, 0x00, 0x1a, '@', '\r', '@'
  };
  int i;
  unsigned int x = seed | 1;
  for (i = 0; i < len; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    buf[i] = (unsigned char)(x >> 8);
  }
  for (i = 0; i < (int)sizeof(tricky) && i < len; i++) buf[i] = tricky[i];
}

static int write_file(const char *path, const unsigned char *data, int len) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  if (len && (int)fwrite(data, 1, len, f) != len) { fclose(f); return 0; }
  return fclose(f) == 0;
}

static int file_matches(const char *path, const unsigned char *data, int len) {
  FILE *f = fopen(path, "rb"); unsigned char *buf; long n; int ok;
  if (!f) return 0;
  buf = malloc((size_t)len + 1);
  n = (long)fread(buf, 1, (size_t)len + 1, f);
  fclose(f);
  ok = (n == len) && (len == 0 || memcmp(buf, data, (size_t)len) == 0);
  free(buf);
  return ok;
}

static void clear_dir(const char *dir) {
  char path[1200]; int i;
  for (i = 0; i < 3; i++) { snprintf(path, sizeof(path), "%s/%s", dir, filename[i]); remove(path); }
  snprintf(path, sizeof(path), "%s/.partial", dir); remove(path);
}

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s (status: %s)\n", msg, last_status); fails++; } } while (0)

/* split "a b c" into argv slots; returns the new count */
static int add_args(char **argv, int n, const char *extra) {
  static char buf[256]; char *p;
  if (!extra || !*extra) return n;
  snprintf(buf, sizeof(buf), "%s", extra);
  for (p = strtok(buf, " "); p; p = strtok(NULL, " ")) argv[n++] = p;
  return n;
}

/* ---- receive from sz ---------------------------------------------------- */
/* idx < 0: send all three files in one batch */
static int run_sz_recv(const char *label, int idx, const char *extra, int crc32, long corrupt, long killat, long cancelat) {
  char path[3][1200];
  char *argv[16]; int n = 0, i, rc, st;
  char msg[256];

  peer_reset(); clear_dir(recvdir);
  begin_count = end_count = abort_count = 0;
  argv[n++] = "sz"; argv[n++] = "-b"; argv[n++] = "-q";
  n = add_args(argv, n, extra);
  for (i = 0; i < 3; i++) snprintf(path[i], sizeof(path[i]), "%s/%s", srcdir, filename[i]);
  if (idx < 0) { for (i = 0; i < 3; i++) argv[n++] = path[i]; }
  else argv[n++] = path[idx];
  argv[n] = NULL;
  zmodem_recv_crc32 = crc32;
  if (!peer_spawn(NULL, argv)) { CHECK(0, "spawn sz"); return 0; }
  corrupt_rx_at = corrupt; kill_peer_at_rx = killat; cancel_at_rx = cancelat;
  rc = zmodem_recv();
  st = peer_wait();
  zmodem_recv_crc32 = 1;
  snprintf(msg, sizeof(msg), "%s: rc=%d sz_exit=%d saved=%d vclock=%ums begin/end/abort=%d/%d/%d",
           label, rc, st, xfer_saved_bytes, vclock, begin_count, end_count, abort_count);
  printf("  [%s]\n", msg);
  return rc;
}

static void test_sz_recv_ok(const char *label, int idx, const char *extra, int crc32, long corrupt) {
  int rc = run_sz_recv(label, idx, extra, crc32, corrupt, -1, -1);
  char path[1200]; int ok, i;
  if (idx < 0) {
    ok = (rc == 3);
    for (i = 0; i < 3; i++) {
      snprintf(path, sizeof(path), "%s/%s", recvdir, filename[i]);
      ok = ok && file_matches(path, filedata[i], filelen[i]);
    }
  } else {
    snprintf(path, sizeof(path), "%s/%s", recvdir, filename[idx]);
    ok = (rc == 1) && file_matches(path, filedata[idx], filelen[idx]);
  }
  CHECK(ok, label);
}

/* ---- send to rz --------------------------------------------------------- */
static int send_files(int first, int count) {
  char path[1200]; int i, ok = 1;
  if (!zmodem_send_begin()) return 0;
  for (i = first; i < first + count && ok; i++) {
    snprintf(path, sizeof(path), "%s/%s", srcdir, filename[i]);
    xfer_sendfile = fopen(path, "rb");
    if (!xfer_sendfile) return 0;
    fseek(xfer_sendfile, 0, SEEK_END); xfer_file_size = (int)ftell(xfer_sendfile); fseek(xfer_sendfile, 0, SEEK_SET);
    ok = zmodem_send_file(path);   /* full path: the sender must strip it */
    fclose(xfer_sendfile); xfer_sendfile = NULL;
  }
  return ok && zmodem_send_end();
}

static void test_rz_send(const char *label, int first, int count, const char *extra, long corrupt) {
  char *argv[16]; int n = 0, rc, st, i, ok;
  char path[1200], msg[256];

  peer_reset(); clear_dir(rzdir);
  argv[n++] = "rz"; argv[n++] = "-b"; argv[n++] = "-q";
  n = add_args(argv, n, extra);
  argv[n] = NULL;
  if (!peer_spawn(rzdir, argv)) { CHECK(0, "spawn rz"); return; }
  corrupt_tx_at = corrupt;
  rc = send_files(first, count);
  st = peer_wait();
  snprintf(msg, sizeof(msg), "%s: rc=%d rz_exit=%d vclock=%ums tx=%ld", label, rc, st, vclock, tx_total);
  printf("  [%s]\n", msg);
  ok = (rc == 1) && (st == 0);
  for (i = first; i < first + count; i++) {
    snprintf(path, sizeof(path), "%s/%s", rzdir, filename[i]);
    ok = ok && file_matches(path, filedata[i], filelen[i]);
  }
  CHECK(ok, label);
}

/* ---- loopback: our sender (child) to our receiver (parent) -------------- */
static void test_loopback(const char *label, int crc32) {
  int p_a[2], p_b[2]; int rc, st, i, ok; char path[1200];

  peer_reset(); clear_dir(recvdir);
  begin_count = end_count = abort_count = 0;
  if (pipe(p_a) || pipe(p_b)) { CHECK(0, "pipes"); return; }
  peer_pid = fork();
  if (peer_pid == 0) {
    /* child: sender. a = child->parent, b = parent->child */
    close(p_a[0]); close(p_b[1]);
    peer_in = p_a[1]; peer_out = p_b[0];
    fcntl(peer_out, F_SETFL, O_NONBLOCK);
    real_time = 1;
    _exit(send_files(0, 3) ? 0 : 1);
  }
  close(p_a[1]); close(p_b[0]);
  peer_in = p_b[1]; peer_out = p_a[0];
  fcntl(peer_out, F_SETFL, O_NONBLOCK);
  real_time = 1;
  zmodem_recv_crc32 = crc32;
  rc = zmodem_recv();
  st = peer_wait();
  zmodem_recv_crc32 = 1;
  printf("  [%s: rc=%d sender_exit=%d vclock=%ums]\n", label, rc, st, vclock);
  ok = (rc == 3) && (st == 0);
  for (i = 0; i < 3; i++) {
    snprintf(path, sizeof(path), "%s/%s", recvdir, filename[i]);
    ok = ok && file_matches(path, filedata[i], filelen[i]);
  }
  CHECK(ok, label);
}

/* ---- scripted peers ------------------------------------------------------ */
static int count_zrinit(void) {
  /* hex ZRINIT header starts "**\x18B01" */
  int i, n = 0;
  for (i = 0; i + 5 < outq_len; i++)
    if (memcmp(outq + i, "**\x18" "B01", 6) == 0) n++;
  return n;
}
static int count_byte(unsigned char b) { int i, n = 0; for (i = 0; i < outq_len; i++) if (outq[i] == b) n++; return n; }

int main(int argc, char **argv) {
  char path[1200]; int i, rc;
  char tmpl[1024];
  const char *tmp = getenv("TMPDIR");
  (void)argc; (void)argv;

  signal(SIGPIPE, SIG_IGN);
  crc_init();
  if (!tmp || !*tmp) tmp = "/tmp";
  snprintf(tmpl, sizeof(tmpl), "%s/cgt-zmodem-XXXXXX", tmp);
  if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
  snprintf(srcdir, sizeof(srcdir), "%s/src", tmpl); mkdir(srcdir, 0700);
  snprintf(recvdir, sizeof(recvdir), "%s/recv", tmpl); mkdir(recvdir, 0700);
  snprintf(rzdir, sizeof(rzdir), "%s/rz", tmpl); mkdir(rzdir, 0700);
  for (i = 0; i < 3; i++) {
    filedata[i] = malloc((size_t)filelen[i] + 1);
    make_data(filedata[i], filelen[i], 0x1234u * (unsigned)(i + 1));
    snprintf(path, sizeof(path), "%s/%s", srcdir, filename[i]);
    if (!write_file(path, filedata[i], filelen[i])) { perror(path); return 1; }
  }
  printf("work dir: %s\n", tmpl);

  puts("[ZMODEM receive from sz]");
  test_sz_recv_ok("300 byte file", 0, "", 1, -1);
  test_sz_recv_ok("100 KB file", 1, "", 1, -1);
  test_sz_recv_ok("0 byte file", 2, "", 1, -1);
  test_sz_recv_ok("batch of all three files", -1, "", 1, -1);
  test_sz_recv_ok("sz -e: ZSINIT + every control char escaped", 1, "-e", 1, -1);
  test_sz_recv_ok("16-bit CRC (receiver without CANFC32)", 1, "", 0, -1);
  test_sz_recv_ok("corrupted byte mid-stream -> ZRPOS resync", 1, "", 1, 50000);
  test_sz_recv_ok("corrupted byte mid-stream, 16-bit CRC", 1, "", 0, 40000);
  test_sz_recv_ok("sz -w 8192: windowed, ZCRCQ subpackets want ZACK", 1, "-w 8192", 1, -1);
  test_sz_recv_ok("sz -l 2048: ZCRCW frame every 2 KB", 1, "-l 2048", 1, -1);
  test_sz_recv_ok("sz -L 256: short subpackets", 1, "-L 256", 1, -1);
  test_sz_recv_ok("sz -w 4096 with a corrupted byte", 1, "-w 4096", 1, 60000);

  puts("[ZMODEM receive: failure paths]");
  rc = run_sz_recv("pipe closed mid-transfer", 1, "", 1, -1, 30000, -1);
  CHECK(rc == 0 && vclock < 20000, "sender dies mid-transfer: rc 0, fails fast, nothing saved");
  CHECK(end_count == 0 && abort_count >= 1, "partial download was discarded");
  rc = run_sz_recv("user cancel mid-transfer", 1, "", 1, -1, -1, 30000);
  CHECK(rc == 0 && vclock < 20000 && end_count == 0, "user cancel: rc 0, partial discarded");

  peer_reset();
  rc = zmodem_recv();
  printf("  [silent peer: rc=%d vclock=%us zrinit_sent=%d]\n", rc, vclock / 1000, count_zrinit());
  CHECK(rc == 0 && vclock >= 100000 && vclock < 130000 && count_zrinit() == 11, "silent peer: 11 ZRINIT then give up after ~110 s");
  CHECK(count_byte(0x18) >= 8, "gave up with a CAN burst");

  peer_reset();
  for (i = 0; i < 8; i++) inq[inq_tail++] = 0x18;
  rc = zmodem_recv();
  CHECK(rc == 0 && vclock < 2000, "remote cancel (8 x CAN): rc 0 immediately");

  peer_reset(); disconnected = 1;
  rc = zmodem_recv();
  CHECK(rc == 0 && vclock < 2000, "disconnected before start: rc 0 immediately");

  peer_reset();
  {
    static const char bogus[] = "**\x18" "B0100000000c8c1\r\x8a\x11";   /* ZRINIT with a bad CRC */
    memcpy(inq, bogus, sizeof(bogus) - 1); inq_tail = (int)sizeof(bogus) - 1;
  }
  rc = zmodem_recv();
  CHECK(rc == 0 && count_zrinit() == 11, "bad header CRC is ignored, then timeout");

  puts("[ZMODEM send to rz]");
  test_rz_send("300 byte file", 0, 1, "", -1);
  test_rz_send("100 KB file", 1, 1, "", -1);
  test_rz_send("0 byte file", 2, 1, "", -1);
  test_rz_send("batch of all three files", 0, 3, "", -1);
  test_rz_send("rz -e: receiver demands ESCCTL", 1, 1, "-e", -1);
  test_rz_send("corrupted byte in our stream -> rz sends ZRPOS", 1, 1, "", 50000);
  test_rz_send("rz -A 4096: receiver declares a buffer size (ZCRCW each subpacket)", 1, 1, "-A 4096", -1);
  test_rz_send("rz -A 4096 with a corrupted byte", 1, 1, "-A 4096", 70000);

  puts("[ZMODEM send: failure paths]");
  peer_reset(); disconnected = 1;
  rc = zmodem_send_begin();
  CHECK(rc == 0 && vclock < 2000, "send_begin to a dead socket: rc 0 immediately");
  peer_reset();
  rc = zmodem_send_begin();
  printf("  [silent receiver: rc=%d vclock=%us]\n", rc, vclock / 1000);
  CHECK(rc == 0 && vclock >= 100000 && vclock < 130000, "silent receiver: give up after ~110 s");

  puts("[ZMODEM loopback: our sender -> our receiver]");
  {
    int reps = getenv("ZM_LOOP_REPEAT") ? atoi(getenv("ZM_LOOP_REPEAT")) : 1;
    for (i = 0; i < reps; i++) {
      test_loopback("32-bit CRC", 1);
      test_loopback("16-bit CRC", 0);
    }
  }

  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
