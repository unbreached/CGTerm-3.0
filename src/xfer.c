#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <SDL.h>
#ifdef WINDOWS
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "net.h"
#include "timer.h"
#include "gfx.h"
#include "menu.h"
#include "xfer.h"
#include "xmodem.h"
#include "punter.h"
#include "rainbow.h"
#include "zmodem.h"
#include "diskimage.h"
#include "dir.h"
#include "fileselector.h"
#include "config.h"
#include "keyboard.h"
#include "ui.h"

#include <stdarg.h>
#include <time.h>

/* Debug log file for transfer troubleshooting (per-user, not /tmp) */
static FILE *dbglog = NULL;
static void dbg(const char *fmt, ...) {
  va_list ap;
  if (!cfg_debugmode) return;
  if (!dbglog) {
    char path[600];
    cfg_user_file(path, sizeof(path), "cgterm-debug.log");
    dbglog = fopen(path, "a");
    if (!dbglog) return;
    fprintf(dbglog, "\n=== NEW SESSION ===\n");
  }
  va_start(ap, fmt);
  vfprintf(dbglog, fmt, ap);
  va_end(ap);
  fflush(dbglog);
}

#define XFER_PATH_MAX 1024

char xfer_tempdlname[XFER_PATH_MAX] = "";
char xfer_tempulname[XFER_PATH_MAX] = "";
char xfer_filename[256];

FILE *xfer_sendfile, *xfer_recvfile;

Direction xfer_direction;
Protocol xfer_protocol;
int xfer_cancel;
static int xfer_batch_mode = 0;   /* Multi Punter / ZMODEM: never re-prompt for names */
static int xfer_batch_files = 0;  /* files received so far in a batch (fallback names) */
static unsigned int xfer_last_elapsed_ms = 0;
static void xfer_trace_flush(void);
static int xfer_confirm_pending = 0;   /* ESC pressed, waiting for Y/N */

static const char *xfer_protocol_name(Protocol p) {
  switch (p) {
  case PROT_XMODEM: return "XMODEM";
  case PROT_XMODEMCRC: return "XMODEM-CRC";
  case PROT_XMODEM1K: return "XMODEM-1K";
  case PROT_PUNTER: return "Punter";
  case PROT_MULTIPUNTER: return "Multi Punter";
  case PROT_RAINBOW: return "Rainbow";
  case PROT_ZMODEM: return "ZMODEM";
  default: return "?";
  }
}

/* One line per completed or failed transfer in the per-user transfer log:
 * date, host, protocol, direction, name, bytes, seconds, cps, result. */
void xfer_log_result(const char *direction, const char *name, long bytes, unsigned int ms, const char *result) {
  FILE *f;
  xfer_trace_flush();
  dbg("--- result: %s %s %ld bytes %s\n", direction, name ? name : "-", bytes, result);
  char path[600];
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  double secs = ms / 1000.0;

  if (!cfg_transferlog) return;
  cfg_user_file(path, sizeof(path), "cgterm-transfers.log");
  if ((f = fopen(path, "a")) == NULL) return;
  fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  %-24s %-12s %-4s %-32s %8ld bytes %7.1f s %5.0f cps  %s\n",
          tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
          tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
          cfg_connect_name[0] ? cfg_connect_name : (cfg_host ? cfg_host : "-"),
          xfer_protocol_name(xfer_protocol), direction, name ? name : "-",
          bytes, secs, secs > 0.05 ? bytes / secs : 0.0, result);
  fclose(f);
}

/* "1234 bytes, 12.3 s, 100 cps" for the completion messages */
static void xfer_summary_string(char *out, size_t sz, long bytes, unsigned int ms) {
  double secs = ms / 1000.0;
  snprintf(out, sz, "%ld bytes, %.1f s, %.0f cps", bytes, secs, secs > 0.05 ? bytes / secs : 0.0);
}
int xfer_saved_bytes;
int xfer_file_size;
unsigned char xfer_buffer[4096];
unsigned int xfer_starttime;
unsigned int xfer_last_kbd_check;

static int xfer_progress_total_for_display(int current, int total) {
  int shown_total = total;

  if (shown_total > 0) {
    if (current > shown_total) {
      current = shown_total;
    }
    return shown_total;
  }

  if (current < 0) {
    current = 0;
  }

  shown_total = current + 1024;
  if (shown_total < 1024) {
    shown_total = 1024;
  }
  return shown_total;
}

void xfer_progress_status(const char *message, int current, int total) {
  int shown_current = current;
  int shown_total = xfer_progress_total_for_display(current, total);

  if (shown_current < 0) {
    shown_current = 0;
  }
  if (total > 0 && shown_current > total) {
    shown_current = total;
  }
  if (shown_current > shown_total) {
    shown_current = shown_total;
  }

  menu_update_xfer_progress((char *)message, shown_current, shown_total);
  gfx_vbl();
}

void xfer_progress(const char *message) {
  xfer_progress_status(message, xfer_saved_bytes, xfer_file_size);
}


int xfer_save_file(char *filename);
void xfer_check_kbd(void);
static void xfer_fix_filename(char *filename);

static void xfer_log_errno(const char *prefix, const char *path) {
  if (path && *path) {
    printf("%s: %s (%s)\n", prefix, path, strerror(errno));
  } else {
    printf("%s: %s\n", prefix, strerror(errno));
  }
}

static void xfer_cleanup_temp_download(void) {
  if (xfer_recvfile != NULL) {
    fclose(xfer_recvfile);
    xfer_recvfile = NULL;
  }
  if (xfer_tempdlname[0]) {
    remove(xfer_tempdlname);
    xfer_tempdlname[0] = 0;
  }
}

static int xfer_create_temp_download(void) {
#ifdef WINDOWS
  char tempPath[MAX_PATH];
  char tempFile[MAX_PATH];

  if (GetTempPathA(MAX_PATH, tempPath) == 0) {
    return 0;
  }
  if (GetTempFileNameA(tempPath, "cgt", 0, tempFile) == 0) {
    return 0;
  }

  strncpy(xfer_tempdlname, tempFile, sizeof(xfer_tempdlname) - 1);
  xfer_tempdlname[sizeof(xfer_tempdlname) - 1] = 0;
  return 1;
#else
  int fd;

  snprintf(xfer_tempdlname, sizeof(xfer_tempdlname), "/tmp/cgterm-download-XXXXXX");
  fd = mkstemp(xfer_tempdlname);
  if (fd < 0) {
    xfer_tempdlname[0] = 0;
    return 0;
  }
  close(fd);
  return 1;
#endif
}

static int xfer_open_temp_download(void) {
  /* a previous download that was never saved (prompt abandoned, write
   * refused) would otherwise be orphaned when its name is overwritten */
  xfer_cleanup_temp_download();
  if (!xfer_create_temp_download()) {
    menu_draw_message("Couldn't create tempfile!");
    menu_show();
    gfx_vbl();
    xfer_log_errno("Could not create tempfile", NULL);
    return 0;
  }

  xfer_recvfile = fopen(xfer_tempdlname, "wb+");
  if (xfer_recvfile == NULL) {
    menu_draw_message("Couldn't open tempfile!");
    menu_show();
    gfx_vbl();
    xfer_log_errno("Could not open tempfile", xfer_tempdlname);
    xfer_cleanup_temp_download();
    return 0;
  }

  return 1;
}

/* Create secure temporary file for uploads */
static int xfer_create_temp_upload(void) {
#ifdef WINDOWS
  char tempPath[1024];
  char tempFile[1088];

  if (GetTempPath(sizeof(tempPath), tempPath) == 0) {
    return 0;
  }

  if (GetTempFileName(tempPath, "cgup", 0, tempFile) == 0) {
    return 0;
  }

  strncpy(xfer_tempulname, tempFile, sizeof(xfer_tempulname) - 1);
  xfer_tempulname[sizeof(xfer_tempulname) - 1] = 0;
  return 1;
#else
  int fd;

  snprintf(xfer_tempulname, sizeof(xfer_tempulname), "/tmp/cgterm-upload-XXXXXX");
  fd = mkstemp(xfer_tempulname);
  if (fd < 0) {
    xfer_tempulname[0] = 0;
    return 0;
  }
  close(fd);
  return 1;
#endif
}

/* Cleanup upload temporary file */
static void xfer_cleanup_temp_upload(void) {
  if (xfer_tempulname[0]) {
    remove(xfer_tempulname);
    xfer_tempulname[0] = 0;
  }
}

static int xfer_copy_file(FILE *from, FILE *to, int bytesleft) {
  unsigned char filebuf[4096];
  int l;

  while (bytesleft > 0) {
    l = bytesleft > (int)sizeof(filebuf) ? (int)sizeof(filebuf) : bytesleft;
    l = (int)fread(filebuf, 1, l, from);
    if (l <= 0) {
      return 0;
    }
    if ((int)fwrite(filebuf, 1, l, to) != l) {
      return 0;
    }
    bytesleft -= l;
  }
  return (bytesleft == 0);
}

static const char *multipunter_ext_from_type(int filetype) {
  switch (filetype) {
  case 1:
    return ".seq";
  case 2:
    return ".prg";
  case 3:
    return ".wp";
  default:
    return "";
  }
}

/* Convert a PC filename to C64 format for upload:
 *   "filename.prg" → "filename,p"
 *   "filename.seq" → "filename,s"
 *   "filename.usr" → "filename,u"
 *   "filename.rel" → "filename,r"
 *   "filename.txt" → "filename"  (no type suffix, let BBS decide)
 *   "filename"     → "filename"  (no change)
 * Also uppercases the name since C64 filenames are uppercase. */
static void pc_to_c64_filename(const char *pcname, char *c64name, size_t c64sz) {
  char *dot;
  size_t i;

  snprintf(c64name, c64sz, "%s", pcname);

  /* Find the last dot for extension */
  dot = strrchr(c64name, '.');
  if (dot && strlen(dot) >= 2) {
    if (strcasecmp(dot, ".prg") == 0) {
      *dot = ','; dot[1] = 'p'; dot[2] = 0;
    } else if (strcasecmp(dot, ".seq") == 0) {
      *dot = ','; dot[1] = 's'; dot[2] = 0;
    } else if (strcasecmp(dot, ".usr") == 0) {
      *dot = ','; dot[1] = 'u'; dot[2] = 0;
    } else if (strcasecmp(dot, ".rel") == 0) {
      *dot = ','; dot[1] = 'r'; dot[2] = 0;
    }
    /* Other extensions: leave as-is, BBS will handle */
  }

  /* Uppercase everything before the comma (C64 filenames are uppercase) */
  for (i = 0; c64name[i] && c64name[i] != ','; i++) {
    if (c64name[i] >= 'a' && c64name[i] <= 'z')
      c64name[i] -= 32;
  }

  /* Truncate to 16 chars (C64 filename limit) plus type suffix */
  {
    char *comma = strchr(c64name, ',');
    if (comma) {
      /* Name part max 16 chars */
      int namelen = (int)(comma - c64name);
      if (namelen > 16) {
        memmove(c64name + 16, comma, strlen(comma) + 1);
      }
    } else {
      if (strlen(c64name) > 16)
        c64name[16] = 0;
    }
  }
}


static void multipunter_sanitize_filename(const char *src, char *dst, size_t dstsz, int filetype, int fileno) {
  size_t di = 0;
  int sawdot = 0;

  if (dstsz == 0) {
    return;
  }

  while (*src == ' ') {
    ++src;
  }

  while (*src && di + 1 < dstsz) {
    unsigned char c = (unsigned char) *src++;

    if (c < 32) {
      continue;
    }
    if (c >= 127) {
      c = '_';   /* DEL, PETSCII graphics, UTF-8: not a portable file name byte */
    }
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      c = '_';
    }
    if (c == '.') {
      sawdot = 1;
    }
    dst[di++] = (char) c;
  }

  while (di && (dst[di - 1] == ' ' || dst[di - 1] == '.')) {
    --di;
  }
  dst[di] = 0;

  if (di == 0) {
    snprintf(dst, dstsz, "mpunter_%03d%s", fileno, multipunter_ext_from_type(filetype));
    return;
  }

  /* Convert C64 filetype suffixes: ,p → .prg, ,s → .seq, etc. */
  xfer_fix_filename(dst);

  /* If still no extension, add one based on the Punter filetype byte */
  if (!sawdot && !strchr(dst, '.') && multipunter_ext_from_type(filetype)[0] &&
      strlen(dst) + strlen(multipunter_ext_from_type(filetype)) + 1 < dstsz) {
    strcat(dst, multipunter_ext_from_type(filetype));
  }
}

static int multipunter_read_announcement(char *namebuf, size_t namebufsz, int *is_batch_end) {
  signed int c;
  size_t pos = 0;

  *is_batch_end = 0;
  if (namebufsz) {
    namebuf[0] = 0;
  }

  xfer_progress("Multi Punter: waiting for filename...");
  gfx_vbl();

  /* Wait for first 0x09 (tab) — BBS sends multiple (typically 10) */
  while (!xfer_cancel) {
    c = xfer_recv_byte(1000);
    if (c == -2) {
      xfer_progress("Disconnected!");
      return 0;
    }
    if (c < 0) {
      gfx_vbl();
      xfer_check_kbd();
      continue;
    }
    if (c == 0x09) {
      break;
    }
  }

  if (xfer_cancel) {
    return 0;
  }

  /* Consume any additional 0x09 bytes and 0x04 (EOT) markers */
  while (!xfer_cancel) {
    c = xfer_recv_byte(1000);
    if (c == -2) {
      xfer_progress("Disconnected!");
      return 0;
    }
    if (c < 0) {
      continue;
    }
    if (c == 0x04) {
      /* Batch end signal — consume any remaining 0x04 bytes */
      while (xfer_recv_byte(100) == 0x04) { /* drain */ }
      *is_batch_end = 1;
      return 1;
    }
    if (c != 0x09) {
      /* First non-tab byte is start of filename */
      namebuf[pos++] = (char) c;
      namebuf[pos] = 0;
      break;
    }
  }

  if (xfer_cancel) {
    return 0;
  }

  /* Read rest of filename — terminated by CR, LF, or null (g$ in C*BASE) */
  while (!xfer_cancel) {
    c = xfer_recv_byte(1000);
    if (c == -2) {
      xfer_progress("Disconnected!");
      return 0;
    }
    if (c < 0) {
      gfx_vbl();
      continue;
    }

    if (c == '\r' || c == '\n' || c == 0x00) {
      namebuf[pos] = 0;

      while ((c = xfer_recv_byte(10)) >= 0) {
        if (c != '\r' && c != '\n') {
          break;
        }
      }

      return 1;
    }

    /* Prevent integer overflow and ensure safe buffer bounds */
    if (pos < namebufsz - 1 && pos < 255) {  /* Added explicit size limit */
      namebuf[pos] = (char) c;
      pos++;
      namebuf[pos] = 0;
    } else {
      /* Buffer full - stop reading filename */
      break;
    }
  }

  return 0;
}

static int multipunter_recv(void) {
  int filecount = 0;
  int failcount = 0;
  char remote_name[256];
  char local_name[256];
  int batch_end = 0;
  char msg[256];

  xfer_filename[0] = 0;
  xfer_batch_mode = 1;

  /* Signal the BBS we're ready. C*BASE bbs.bas line 3690 waits for any
   * byte from the terminal before starting the multi download. */
  net_send(0x0d);

  while (!xfer_cancel) {
    if (!multipunter_read_announcement(remote_name, sizeof(remote_name), &batch_end)) {
      xfer_cleanup_temp_download();
      xfer_batch_mode = 0;
      menu_draw_message("Multi Punter failed");
      menu_show();
      gfx_vbl();
      return 0;
    }

    if (batch_end) {
      xfer_batch_mode = 0;
      if (filecount == 0 && failcount == 0) {
        menu_draw_message("Multi Punter: empty batch");
        menu_show();
        gfx_vbl();
        return 0;
      }
      if (failcount) {
        snprintf(msg, sizeof(msg), "Received %d file%s, %d NOT saved (bad name/write error)",
                 filecount, filecount == 1 ? "" : "s", failcount);
      } else {
        snprintf(msg, sizeof(msg), "Received %d Multi Punter file%s", filecount, filecount == 1 ? "" : "s");
      }
      menu_draw_message(msg);
      menu_show();
      gfx_vbl();
      return 1;
    }

    if (!xfer_open_temp_download()) {
      xfer_batch_mode = 0;
      return 0;
    }

    snprintf(msg, sizeof(msg), "Multi Punter: %s (%d)", remote_name, filecount + 1);
    menu_draw_xfer_progress(remote_name, xfer_direction, xfer_protocol);
    menu_show();
    gfx_vbl();

    xfer_saved_bytes = 0;
    xfer_starttime = timer_get_ticks();
    xfer_batch_files = 0;

    if (!punter_recv()) {
      xfer_cleanup_temp_download();
      xfer_batch_mode = 0;
      menu_draw_message("Multi Punter receive failed");
      menu_show();
      gfx_vbl();
      return 0;
    }

    fclose(xfer_recvfile);
    xfer_recvfile = NULL;

    multipunter_sanitize_filename(remote_name, local_name, sizeof(local_name), punter_last_filetype, filecount + failcount + 1);
    snprintf(xfer_filename, 256, "%s", local_name);
    xfer_last_elapsed_ms = timer_get_ticks() - xfer_starttime;
    if (xfer_save_file(local_name) == 1) {
      ++filecount;
    } else {
      ++failcount;
      printf("Multi Punter: could not save '%s'\n", local_name);
    }
    /* Reclaim the temp file before the next iteration's mkstemp overwrites the
     * name — the per-file save paths only remove it on success, so a rejected
     * filename or write error would otherwise orphan it in the temp dir. */
    xfer_cleanup_temp_download();
  }

  xfer_cleanup_temp_download();
  xfer_batch_mode = 0;
  menu_draw_message("Multi Punter cancelled");
  menu_show();
  gfx_vbl();
  return 0;
}


/* Polled from the byte I/O functions during a transfer. ESC asks for a
 * confirmation but does NOT block: the protocol keeps consuming bytes and
 * answering the peer while the question is on screen, so a slow "N" can no
 * longer time out the other side. */
void xfer_check_kbd(void) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      exit(1);
      break;
    case SDL_KEYDOWN:
      if (xfer_confirm_pending) {
        xfer_confirm_pending = 0;
        if (event.key.keysym.sym == SDLK_y) {
          xfer_cancel = 1;
        } else {
          /* any other key = keep going: put the progress window back */
          menu_draw_xfer_progress(xfer_filename, xfer_direction, xfer_protocol);
          menu_show();
          gfx_vbl();
        }
      } else if (event.key.keysym.sym == SDLK_ESCAPE) {
        xfer_confirm_pending = 1;
        menu_draw_message("Cancel transfer? (Y/N)");
        menu_show();
        gfx_vbl();
      }
      break;
    }
  }
}

/* Byte-level transfer trace for debug mode (-b / debug = yes): every byte in
 * both directions goes to the debug log as hex, one line per direction
 * change with the elapsed time, plus the ASCII of the line. This is what
 * we need from a user whose board hangs or crashes during a transfer. */
static char trace_dir = 0;
static char trace_hex[200];
static char trace_asc[40];
static int trace_n = 0;
static unsigned int trace_t0 = 0;

static void xfer_trace_flush(void) {
  if (trace_n == 0) return;
  trace_asc[trace_n] = 0;
  dbg("%c %6u.%03u %-48s |%s|\n", trace_dir, (timer_get_ticks() - trace_t0) / 1000,
      (timer_get_ticks() - trace_t0) % 1000, trace_hex, trace_asc);
  trace_n = 0;
  trace_hex[0] = 0;
}

void xfer_trace_start(const char *what) {
  if (!cfg_debugmode) return;
  xfer_trace_flush();
  trace_t0 = timer_get_ticks();
  dbg("=== %s host=%s proto=%s dir=%s\n", what, cfg_host ? cfg_host : "?",
      xfer_protocol_name(xfer_protocol), xfer_direction == DIR_SEND ? "send" : "recv");
}

static void xfer_trace(char dir, unsigned char c) {
  if (!cfg_debugmode) return;
  if (dir != trace_dir || trace_n >= 16) {
    xfer_trace_flush();
    trace_dir = dir;
  }
  snprintf(trace_hex + strlen(trace_hex), sizeof(trace_hex) - strlen(trace_hex), "%02x ", c);
  trace_asc[trace_n++] = (c >= 32 && c < 127) ? (char)c : '.';
}

void xfer_send_byte(unsigned char c) {
  unsigned int t;

  t = timer_get_ticks();
  if (t > xfer_last_kbd_check + 20) {
    xfer_last_kbd_check = t;
    xfer_check_kbd();
  }
  xfer_trace('T', c);
  net_send(c);
  menu_xfer_feed_byte(c);
}

static int xfer_recv_debug_count = 0;

signed int xfer_recv_byte(int timeout) {
  signed int c;
  unsigned int starttime;

  xfer_recv_debug_count = 0;
  starttime = timer_get_ticks();
  while ((c = net_receive_raw()) == -1) {
    if (timer_get_ticks() > starttime + timeout) {
      if (xfer_recv_debug_count == 0) {
        dbg(" xfer_recv_byte: TIMEOUT after %dms (no data received yet)\n", timeout);
      }
      return(-1);
    } else {
      timer_delay(1);
      if (timer_get_ticks() > xfer_last_kbd_check + 20) {
        xfer_check_kbd();
        xfer_last_kbd_check = timer_get_ticks();
      }
    }
  }

  if (c < 0) {
    if (c == -2) {
      xfer_trace_flush();
      dbg("--- disconnected\n");
    }
    return(c);   /* -2: disconnected (don't feed it to the hex display) */
  }
  xfer_trace('R', (unsigned char)c);
  xfer_recv_debug_count++;
  menu_xfer_feed_byte((unsigned char)c);
  return(c);
}

signed int xfer_recv_byte_error(int timeout, int errorcnt) {
  signed int c;

  while ((c = xfer_recv_byte(timeout)) == -1 && errorcnt) {
    --errorcnt;
  }
  return(c);
}

int xfer_save_data(unsigned char *data, int length) {
  int l;
  int written = 0;

  /* reject negative lengths and cap total download size so a malicious
     server cannot stream an unbounded file (also guards the int counter
     against overflow). */
  if (length < 0 || xfer_saved_bytes > XFER_MAX_DOWNLOAD - length) {
    xfer_progress_status("Download exceeds maximum size", xfer_saved_bytes, xfer_file_size);
    return(0);
  }

  while (written < length) {
    l = (int)fwrite(data + written, 1, length - written, xfer_recvfile);
    if (l > 0) {
      written += l;
      continue;
    }

    xfer_log_errno("xfer_save_data failed", xfer_tempdlname);
    return(0);
  }

  if (fflush(xfer_recvfile) != 0) {
    xfer_log_errno("xfer_save_data flush failed", xfer_tempdlname);
    return(0);
  }

  xfer_saved_bytes += written;
  return(written);
}

int xfer_load_data(unsigned char *data, int length) {
  int l;
  int read = 0;

  while (read < length) {
    if ((l = (int)fread(data + read, 1, length - read, xfer_sendfile))) {
      read += l;
    } else {
      if (ferror(xfer_sendfile)) {
        xfer_log_errno("xfer_load_data failed", NULL);
        return(0);
      } else {
        return(read);
      }
    }
  }
  return(read);
}

int xfer_recv(void) {
  int status = 0;

  xfer_trace_start("xfer_recv");
  dbg(" xfer_recv() entered: protocol=%d direction=%d\n", xfer_protocol, xfer_direction);
  dbg(" dldir='%s' xferdir='%s'\n", cfg_dldir, cfg_xferdir);
  dbg(" net_connected=%d\n", net_connected());

  xfer_filename[0] = 0;
  xfer_cancel = 0;
  xfer_confirm_pending = 0;
  xfer_saved_bytes = 0;
  xfer_starttime = timer_get_ticks();
  menu_draw_xfer_progress("No filename", xfer_direction, xfer_protocol);
  menu_show();
  gfx_vbl();

  if (xfer_protocol == PROT_MULTIPUNTER) {
    dbg(" starting multipunter_recv\n");
    return multipunter_recv();
  }

  if (!xfer_open_temp_download()) {
    dbg(" xfer_open_temp_download FAILED\n");
    return 0;
  }
  dbg(" temp download opened OK\n");

  if (xfer_protocol == PROT_XMODEM) {
    dbg(" starting xmodem_recv(checksum)\n");
    status = xmodem_recv(0);
  } else if (xfer_protocol == PROT_XMODEMCRC) {
    dbg(" starting xmodem_recv(CRC)\n");
    status = xmodem_recv(1);
  } else if (xfer_protocol == PROT_XMODEM1K) {
    dbg(" starting xmodem_recv(1K)\n");
    status = xmodem_recv(1);
  } else if (xfer_protocol == PROT_PUNTER) {
    dbg(" starting punter_recv\n");
    status = punter_recv();
    /* Flag that we need to send Ctrl-X after save dialog completes */
  } else if (xfer_protocol == PROT_RAINBOW) {
    dbg(" starting rainbow_recv\n");
    status = rainbow_recv();
  } else if (xfer_protocol == PROT_ZMODEM) {
    /* batch receive: files are opened, streamed and saved by the protocol
     * through xfer_begin_file()/xfer_end_file(); nothing to prompt for */
    int files;
    xfer_cleanup_temp_download();   /* the generic temp file is not used */
    files = zmodem_recv();
    xfer_batch_mode = 0;
    if (files > 0) {
      char msg[96];
      snprintf(msg, sizeof(msg), "Received %d ZMODEM file%s", files, files == 1 ? "" : "s");
      menu_draw_message(msg);
    } else {
      menu_draw_message(xfer_cancel ? "ZMODEM cancelled" : "ZMODEM receive failed");
    }
    menu_show();
    gfx_vbl();
    xfer_last_elapsed_ms = timer_get_ticks() - xfer_starttime;
    return files > 0;
  }
  dbg(" xfer_recv result: status=%d\n", status);
  xfer_last_elapsed_ms = timer_get_ticks() - xfer_starttime;

  if (xfer_recvfile != NULL) {
    fclose(xfer_recvfile);
    xfer_recvfile = NULL;
  }

  if (!status) {
    xfer_log_result("recv", xfer_filename[0] ? xfer_filename : NULL, xfer_saved_bytes,
                    xfer_last_elapsed_ms, xfer_cancel ? "cancelled" : "FAILED");
    xfer_cleanup_temp_download();
  }

  return(status);
}


/* ---- per-file hooks for batch protocols (ZMODEM) ----
 * The protocol opens a temp file, streams data through xfer_save_data() and
 * ends the file with the remote name; the name is sanitised and saved
 * without prompting, exactly like a Multi Punter file. */
static unsigned int xfer_file_start = 0;

int xfer_begin_file(void) {
  xfer_batch_mode = 1;
  xfer_saved_bytes = 0;
  xfer_file_start = timer_get_ticks();
  return xfer_open_temp_download();
}

int xfer_end_file(const char *remote_name) {
  char local_name[256];
  int rc;
  unsigned int ms = timer_get_ticks() - xfer_file_start;

  if (xfer_recvfile) {
    fclose(xfer_recvfile);
    xfer_recvfile = NULL;
  }
  /* file type 0: a ZMODEM name is a host name already, don't append .prg */
  multipunter_sanitize_filename(remote_name ? remote_name : "", local_name, sizeof(local_name), 0, ++xfer_batch_files);
  snprintf(xfer_filename, 256, "%s", local_name);
  xfer_last_elapsed_ms = ms;
  rc = (xfer_save_file(local_name) == 1);
  if (!rc) {
    xfer_log_result("recv", local_name, xfer_saved_bytes, ms, "NOT SAVED");
  }
  xfer_cleanup_temp_download();
  return rc;
}

void xfer_abort_file(void) {
  if (xfer_recvfile) {
    fclose(xfer_recvfile);
    xfer_recvfile = NULL;
  }
  xfer_log_result("recv", xfer_filename[0] ? xfer_filename : NULL, xfer_saved_bytes,
                  timer_get_ticks() - xfer_file_start, xfer_cancel ? "cancelled" : "FAILED");
  xfer_cleanup_temp_download();
}

/* Extract 'src' from the image into 'dest'. When exactraw is given it is the
 * entry's own 16-byte PETSCII name (from the directory listing) and is
 * matched byte-for-byte, so names with '*', '?' or graphics characters work
 * and any file type (PRG/SEQ/USR) is accepted. */
int xfer_copy_from_image(char *imgname, const char *src, const unsigned char *exactraw, char *dest) {
  DiskImage *di;
  ImageFile *imgfile = NULL;
  unsigned char rawname[16];
  FILE *outh;
  unsigned char buffer[4096];
  int l;
  int total = 0;

  dbg("D64-EXTRACT: image='%s' file='%s' dest='%s'\n", imgname, src, dest);

  if ((di = di_load_image(imgname)) == NULL) {
    dbg("D64-EXTRACT: FAILED to load image '%s'\n", imgname);
    return(0);
  }

  if (exactraw) {
    imgfile = di_open_exact(di, exactraw);
  }
  if (imgfile == NULL) {
    di_rawname_from_name(rawname, (char *)src);
    dbg("D64-EXTRACT: rawname=[%02x %02x %02x %02x ...]\n",
        rawname[0], rawname[1], rawname[2], rawname[3]);
    if ((imgfile = di_open(di, rawname, T_PRG, "rb")) == NULL &&
        (imgfile = di_open(di, rawname, T_SEQ, "rb")) == NULL &&
        (imgfile = di_open(di, rawname, T_USR, "rb")) == NULL) {
      dbg("D64-EXTRACT: FAILED to open '%s' in image\n", src);
      di_free_image(di);
      return(0);
    }
  }

  if ((outh = fopen(dest, "wb")) == NULL) {
    dbg("D64-EXTRACT: FAILED to create temp file '%s'\n", dest);
    di_close(imgfile);
    di_free_image(di);
    return(0);   /* callers test truthiness; -1 would read as success */
  }

  while ((l = (int)di_read(imgfile, buffer, 4096))) {
    if ((int)fwrite(buffer, 1, l, outh) != l) {
      dbg("D64-EXTRACT: write error at %d bytes\n", total);
      di_close(imgfile);
      di_free_image(di);
      fclose(outh);
      return(0);
    }
    total += l;
  }

  dbg("D64-EXTRACT: OK, extracted %d bytes\n", total);
  di_close(imgfile);
  di_free_image(di);
  fclose(outh);
  return(1);
}

/* Punter filetype byte for an upload, from the local name: ",s"/".seq" is
 * SEQ (1), everything else PRG (2). */
static int punter_filetype_from_name(const char *name) {
  size_t len = strlen(name);

  if (len >= 2 && name[len - 2] == ',' && (name[len - 1] == 's' || name[len - 1] == 'S')) {
    return 1;
  }
  if (len >= 4 && strcasecmp(name + len - 4, ".seq") == 0) {
    return 1;
  }
  return 2;
}

int xfer_send(const char *filename, const unsigned char *rawname) {
  char name[1088];
  int deletetmp = 0;
  int ok = 0;

  xfer_cancel = 0;
  xfer_confirm_pending = 0;
  xfer_starttime = timer_get_ticks();
  xfer_trace_start(filename);
  menu_draw_xfer_progress((char *)filename, xfer_direction, xfer_protocol);
  menu_show();
  gfx_vbl();

  if (path_is_disk_image(cfg_xferdir)) {
    /* Create secure temporary file for upload */
    if (xfer_create_temp_upload() && xfer_copy_from_image(cfg_xferdir, filename, rawname, xfer_tempulname)) {
      snprintf(name, sizeof(name), "%s", xfer_tempulname);
      deletetmp = 1;
    } else {
      xfer_cleanup_temp_upload();   /* mkstemp may already have created it */
      menu_draw_message("Couldn't open file!");
      menu_show();
      printf("Attempt to open file (%s) failed in xfer_send()\n", filename);
      gfx_vbl();
      return 0;
    }
  } else {
    snprintf(name, sizeof(name), "%s%c%s", cfg_xferdir,
#ifdef WINDOWS
      '\\',
#else
      '/',
#endif
      filename);
  }

  if ((xfer_sendfile = fopen(name, "rb"))) {
    if (fseek(xfer_sendfile, 0, SEEK_END)) {
      fclose(xfer_sendfile);
      if (deletetmp) xfer_cleanup_temp_upload();
      menu_draw_message("Couldn't read file size!");
      menu_show();
      gfx_vbl();
      return 0;
    }
    xfer_file_size = (int)ftell(xfer_sendfile);
    fseek(xfer_sendfile, 0, SEEK_SET);
    punter_send_filetype = punter_filetype_from_name(filename);

    if (xfer_protocol == PROT_XMODEM) {
      ok = xmodem_send(0);
    } else if (xfer_protocol == PROT_XMODEMCRC) {
      ok = xmodem_send(0);
    } else if (xfer_protocol == PROT_XMODEM1K) {
      ok = xmodem_send(1);
    } else if (xfer_protocol == PROT_PUNTER) {
      ok = punter_send();
    } else if (xfer_protocol == PROT_RAINBOW) {
      ok = rainbow_send(filename);
    } else if (xfer_protocol == PROT_MULTIPUNTER) {
      /* Multi Punter send is handled by xfer_send_multipunter() */
    }
    fclose(xfer_sendfile);

    xfer_last_elapsed_ms = timer_get_ticks() - xfer_starttime;
    xfer_log_result("send", filename, ok ? (long)xfer_file_size : (long)xfer_saved_bytes,
                    xfer_last_elapsed_ms, ok ? "ok" : (xfer_cancel ? "cancelled" : "FAILED"));
    /* Show completion message with auto-dismiss; say so when it failed */
    if (ok) {
      char sum[96], msg[200];
      xfer_summary_string(sum, sizeof(sum), xfer_file_size, xfer_last_elapsed_ms);
      snprintf(msg, sizeof(msg), "Upload complete: %s", sum);
      menu_draw_message_timed(msg, 5000);
    } else {
      menu_draw_message_timed(xfer_cancel ? "Upload cancelled" : "Upload FAILED", 5000);
    }
  } else {
    menu_draw_message("Couldn't open file!");
    menu_show();
    gfx_vbl();
  }

  if (deletetmp) {
    xfer_cleanup_temp_upload();
  }
  return ok;
}

/* Returns 1 when the file is in the image and the temp file is gone. */
int xfer_save_file_in_image(char *filename) {
  FILE *from;
  DiskImage *di;
  ImageFile *to;
  unsigned char filebuf[4096];
  char msgbuf[4096];
  int bytesleft, l;
  unsigned char rawname[16];
  FileType ftype = T_PRG;
  char cleanname[256];
  int ok = 0;

  dbg("D64-SAVE: filename='%s' dldir='%s' bytes=%d\n", filename, cfg_dldir, xfer_saved_bytes);
  char *comma;

  /* Copy filename and detect C64 filetype from ",X" suffix */
  snprintf(cleanname, sizeof(cleanname), "%s", filename);
  comma = strrchr(cleanname, ',');
  if (comma && strlen(comma) == 2) {
    switch (comma[1]) {
    case 'p': case 'P': ftype = T_PRG; break;
    case 's': case 'S': ftype = T_SEQ; break;
    case 'u': case 'U': ftype = T_USR; break;
    }
    *comma = 0;  /* Strip the ",X" suffix for the D64 name */
  }
  /* Also strip PC extensions if present (.prg, .seq, etc.) */
  {
    char *dot = strrchr(cleanname, '.');
    if (dot && strlen(dot) == 4) {
      if (strcasecmp(dot, ".prg") == 0) { ftype = T_PRG; *dot = 0; }
      else if (strcasecmp(dot, ".seq") == 0) { ftype = T_SEQ; *dot = 0; }
      else if (strcasecmp(dot, ".usr") == 0) { ftype = T_USR; *dot = 0; }
      else if (strcasecmp(dot, ".rel") == 0) { ftype = T_REL; *dot = 0; }
    }
  }

  if ((di = di_load_image(cfg_dldir)) == NULL) {
    menu_draw_message("Couldn't open disk image");
    menu_show();
    gfx_vbl();
    return 0;
  }

  /* Check disk space before writing */
  {
    int blocks_needed = (xfer_saved_bytes + 253) / 254;
    dbg("D64-SAVE: image='%s' file='%s' ftype=%d bytes=%d blocks_needed=%d blocks_free=%d\n",
        cfg_dldir, cleanname, ftype, xfer_saved_bytes, blocks_needed, di->blocksfree);
    if (blocks_needed > di->blocksfree) {
      char fullmsg[128];
      snprintf(fullmsg, sizeof(fullmsg), "Disk full! Need %d blocks, %d free. Change path (J) and retry.",
               blocks_needed, di->blocksfree);
      di_free_image(di);
      menu_draw_message(fullmsg);
      menu_show();
      gfx_vbl();
      /* Don't remove temp file — user can change disk and retry save */
      return 0;
    }
  }

  if ((from = fopen(xfer_tempdlname, "rb")) == NULL) {
    di_free_image(di);
    menu_draw_message("Couldn't open temp file!");
    menu_show();
    xfer_log_errno("Could not open temp file", xfer_tempdlname);
    gfx_vbl();
    return 0;
  }

  di_rawname_from_name(rawname, cleanname);

  to = di_open(di, rawname, ftype, "wb");
  if (to == NULL) {
    int st = di->status;
    fclose(from);
    di_free_image(di);
    menu_draw_message(st == 63 ? "File exists in image! Use another name." : "Couldn't write file!");
    menu_show();
    gfx_vbl();
    return 0;
  }

  bytesleft = xfer_saved_bytes;
  while (bytesleft > 0) {
    l = bytesleft > (int)sizeof(filebuf) ? (int)sizeof(filebuf) : bytesleft;
    l = (int)fread(filebuf, 1, l, from);
    if (l <= 0) {
      menu_draw_message("Read error!");
      goto done;
    }
    if (di_write(to, filebuf, l) != l) {
      menu_draw_message("Disk full! Change path (J) and retry.");
      /* Don't remove temp file */
      goto done;
    }
    bytesleft -= l;
  }

  fclose(from);
  di_close(to);
  /* Only now is the data really on disk; a failed write-back keeps the
   * temp file so the user can retry with another image. */
  if (di_free_image(di) != 0) {
    menu_draw_message("Could not write disk image! Download kept.");
    menu_show();
    gfx_vbl();
    xfer_log_errno("Could not write disk image", cfg_dldir);
    return 0;
  }
  remove(xfer_tempdlname);
  xfer_tempdlname[0] = 0;

  dbg("D64-SAVE: SUCCESS - saved '%s' (%d bytes) into '%s'\n", cleanname, xfer_saved_bytes, cfg_dldir);
  {
    char sum[96];
    xfer_summary_string(sum, sizeof(sum), xfer_saved_bytes, xfer_last_elapsed_ms);
    snprintf(msgbuf, sizeof(msgbuf), "Saved %s to image (%s)", cleanname, sum);
  }
  xfer_log_result("recv", cleanname, xfer_saved_bytes, xfer_last_elapsed_ms, "ok (disk image)");
  menu_draw_message_timed(msgbuf, 5000);
  return 1;

 done:
  menu_show();
  gfx_vbl();
  fclose(from);
  di_close(to);   /* rolls back the incomplete entry and its blocks */
  di_free_image(di);
  return ok;
}

static int xfer_ensure_dldir(void) {
  struct stat st;

  if (stat(cfg_dldir, &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      return 1;  /* exists */
    }
    printf("Download path is not a directory: %s\n", cfg_dldir);
    getcwd(cfg_dldir, 256);
    printf("Using fallback: %s\n", cfg_dldir);
    return 1;
  }

  /* Directory doesn't exist — try to create it */
  printf("Download directory does not exist: %s\n", cfg_dldir);
  printf("Creating download directory...\n");

#ifdef WINDOWS
  if (_mkdir(cfg_dldir) == 0) {
#else
  if (mkdir(cfg_dldir, 0755) == 0) {
#endif
    printf("Created: %s\n", cfg_dldir);
    return 1;
  }

  printf("Could not create %s: %s\n", cfg_dldir, strerror(errno));
  /* Fall back to current directory */
  getcwd(cfg_dldir, 256);
  printf("Using fallback: %s\n", cfg_dldir);
  return 1;
}

/* Validate filename to prevent path traversal attacks */
static int xfer_validate_filename(const char *filename) {
  const char *p;

  if (!filename || strlen(filename) == 0 || strlen(filename) > 255) {
    return 0;
  }

  /* Block path traversal sequences */
  if (strstr(filename, "..") || strstr(filename, "/.") || strstr(filename, "\\.")) {
    return 0;
  }

  /* Block absolute paths and directory separators */
  if (strchr(filename, '/') || strchr(filename, '\\') || filename[0] == '/') {
    return 0;
  }

  /* Block null bytes and control characters */
  for (p = filename; *p; p++) {
    unsigned char ch = (unsigned char)*p;   /* char is signed on x86/arm64 macOS */
    if (ch < 32 || ch == 127) {
      return 0;
    }
  }

  /* Block Windows reserved device names (case-insensitive, ignoring extension).
   * Names like CON.txt, com1.log, lpt9, NUL are all reserved on Windows. */
  {
    char stem[16];
    int sl = 0;
    const char *q = filename;
    while (*q && *q != '.' && sl < (int)sizeof(stem) - 1) {
      char ch = *q++;
      if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
      stem[sl++] = ch;
    }
    stem[sl] = 0;

    if (strcmp(stem, "con") == 0 || strcmp(stem, "prn") == 0 ||
        strcmp(stem, "aux") == 0 || strcmp(stem, "nul") == 0) {
      return 0;
    }
    /* com1..com9, lpt1..lpt9 */
    if (sl == 4 && (memcmp(stem, "com", 3) == 0 || memcmp(stem, "lpt", 3) == 0)
        && stem[3] >= '1' && stem[3] <= '9') {
      return 0;
    }
  }

  return 1;
}

/* Returns 1 saved, 0 failed, -1 when the user has been re-prompted. */
int xfer_save_file_in_dir(char *filename) {
  FILE *from, *to;
  char msgbuf[4096];
  char name[1088];
  char safe_filename[256];

  if (!xfer_ensure_dldir()) {
    menu_draw_message("No download directory!");
    menu_show();
    gfx_vbl();
    return 0;
  }

  /* Validate and sanitize filename */
  if (!xfer_validate_filename(filename)) {
    if (xfer_batch_mode) {
      snprintf(msgbuf, sizeof(msgbuf), "Invalid filename blocked: %.240s", filename);
      menu_draw_message(msgbuf);
      menu_show();
      gfx_vbl();
      return 0;
    }
    /* Interactive: ask again instead of silently losing the download. */
    xfer_filename[0] = 0;
    ui_inputcall(30, "Bad name (no / \\ ..). File name:", xfer_filename,
                 (void (*)(char *))&xfer_save_file, FOCUS_REQUESTER);
    ui_inputcall_on_cancel(&xfer_discard_download);
    return -1;
  }

  /* Create safe copy of filename */
  snprintf(safe_filename, sizeof(safe_filename), "%.240s", filename);

  if ((from = fopen(xfer_tempdlname, "rb")) == NULL) {
    menu_draw_message("Couldn't open temp file!");
    menu_show();
    xfer_log_errno("Could not open temp file", xfer_tempdlname);
    gfx_vbl();
    return 0;
  }

  snprintf(name, sizeof(name), "%s%c%s", cfg_dldir,
#ifdef WINDOWS
    '\\',
#else
    '/',
#endif
    safe_filename);

  /* Refuse to overwrite an existing file — append .1, .2, ... until free. */
  {
    FILE *probe = fopen(name, "rb");
    if (probe != NULL) {
      char altname[1100];
      int suffix;
      fclose(probe);
      for (suffix = 1; suffix < 1000; suffix++) {
        snprintf(altname, sizeof(altname), "%s.%d", name, suffix);
        probe = fopen(altname, "rb");
        if (probe == NULL) {
          snprintf(name, sizeof(name), "%s", altname);
          break;
        }
        fclose(probe);
      }
      if (suffix >= 1000) {
        fclose(from);
        menu_draw_message("File exists, too many duplicates!");
        menu_show();
        gfx_vbl();
        return 0;
      }
    }
  }

  to = fopen(name, "wb");
  if (to == NULL) {
    fclose(from);
    menu_draw_message("Couldn't write file!");
    menu_show();
    xfer_log_errno("Could not open destination file", name);
    gfx_vbl();
    return 0;
  }

  if (!xfer_copy_file(from, to, xfer_saved_bytes) || fclose(to) != 0) {
    menu_draw_message("Write error!");
    menu_show();
    gfx_vbl();
    fclose(from);
    remove(name);   /* don't leave a truncated file behind */
    return 0;
  }

  fclose(from);
  remove(xfer_tempdlname);
  xfer_tempdlname[0] = 0;

  {
    char sum[96];
    xfer_summary_string(sum, sizeof(sum), xfer_saved_bytes, xfer_last_elapsed_ms);
    snprintf(msgbuf, sizeof(msgbuf), "Saved %s (%s)", filename, sum);
  }
  xfer_log_result("recv", filename, xfer_saved_bytes, xfer_last_elapsed_ms, "ok");
  menu_draw_message_timed(msgbuf, 5000);
  return 1;
}

/*
 * Convert C64 filetype suffixes to PC extensions:
 *   ,p → .prg    ,s → .seq    ,u → .usr    ,l → .rel
 * Also handles uppercase variants.
 */
static void xfer_fix_filename(char *filename) {
  size_t len = strlen(filename);
  char *comma;

  if (len < 3) return;

  /* Find last comma */
  comma = strrchr(filename, ',');
  if (comma && strlen(comma) == 2) {
    char type = comma[1];
    const char *ext = NULL;

    switch (type) {
    case 'p': case 'P': ext = ".prg"; break;
    case 's': case 'S': ext = ".seq"; break;
    case 'u': case 'U': ext = ".usr"; break;
    case 'l': case 'L':
    case 'r': case 'R': ext = ".rel"; break;
    }
    if (ext) {
      /* Replace ",X" with ".ext" */
      if ((size_t)(comma - filename) + 4 < 256) {
        strcpy(comma, ext);
      }
    }
  }
}

int xfer_save_file(char *filename) {
  int rc;

  /* Check if download target is a disk image (.d64/.d71/.d81) */
  if (path_is_disk_image(cfg_dldir)) {
    /* Save into D64 — use C64 name, don't apply PC extensions */
    rc = xfer_save_file_in_image(filename);
  } else {
    /* Save to filesystem — convert C64 filetype to PC extension */
    xfer_fix_filename(filename);
    rc = xfer_save_file_in_dir(filename);
  }

  if (rc >= 0) {
    /* Return focus to terminal so the main loop processes BBS responses
     * (not when a new name prompt has just been opened) */
    kbd_focus = FOCUS_TERM;
  }
  return rc;
}


void xfer_discard_download(void) {
  if (xfer_tempdlname[0]) {
    printf("Download discarded: %s\n", xfer_tempdlname);
  }
  xfer_cleanup_temp_download();
  menu_draw_message_timed("Download discarded", 3000);
}


void xfer_send_multipunter(FileSelector *fs) {
  DirEntry *de;
  char name[1088];
  char msg[256];
  int filecount = 0;
  int total_tagged = fs->numtagged;
  char *p;
  int deletetmp;

  xfer_cancel = 0;
  xfer_confirm_pending = 0;
  xfer_starttime = timer_get_ticks();
  xfer_trace_start("multipunter send");

  /* Loop through all entries, send tagged ones */
  de = fs->dir->firstentry;
  while (de && !xfer_cancel) {
    int from_image;
    int aborted = 0;

    if (!de->tagged) {
      de = de->next;
      continue;
    }
    if (!de->name) {
      /* make_name() returns NULL on OOM — skip rather than pass NULL to %s */
      de = de->next;
      continue;
    }

    /* Determine if we're sending from a disk image or filesystem */
    deletetmp = 0;
    from_image = path_is_disk_image(cfg_xferdir);
    (void)p;

    snprintf(msg, sizeof(msg), "Multi Punter: %s (%d/%d)",
             de->name, filecount + 1, total_tagged);
    menu_draw_xfer_progress(de->name, xfer_direction, xfer_protocol);
    menu_show();
    gfx_vbl();

    /* Open the file FIRST. Announcing a name and then failing to open the
     * file would leave the BBS inside a Punter receive that never starts,
     * and the next announcement would land in the middle of it. */
    dbg("MP-SEND: cfg_xferdir='%s'\n", cfg_xferdir);
    if (from_image) {
      dbg("MP-SEND: extracting '%s' from image '%s'\n", de->name, cfg_xferdir);
      /* Create secure temporary file for upload */
      if (xfer_create_temp_upload() && xfer_copy_from_image(cfg_xferdir, de->name, de->rawname, xfer_tempulname)) {
        snprintf(name, sizeof(name), "%s", xfer_tempulname);
        deletetmp = 1;
      } else {
        xfer_cleanup_temp_upload();
        snprintf(msg, sizeof(msg), "Couldn't open %s", de->name);
        menu_draw_message(msg);
        menu_show();
        gfx_vbl();
        de = de->next;
        continue;
      }
    } else {
      snprintf(name, sizeof(name), "%s%c%s", cfg_xferdir,
#ifdef WINDOWS
        '\\',
#else
        '/',
#endif
        de->name);
    }

    dbg(" MP-SEND: opening file '%s'\n", name);
    if ((xfer_sendfile = fopen(name, "rb"))) {
      int send_result;

      if (fseek(xfer_sendfile, 0, SEEK_END)) {
        dbg(" MP-SEND: fseek failed!\n");
        fclose(xfer_sendfile);
        if (deletetmp) xfer_cleanup_temp_upload();
        de = de->next;
        continue;
      }
      xfer_file_size = (int)ftell(xfer_sendfile);
      fseek(xfer_sendfile, 0, SEEK_SET);
      xfer_saved_bytes = 0;
      punter_send_filetype = punter_filetype_from_name(de->name);

      /* Send file announcement: 0x09 + filename + \r */
      {
        char c64name[256];
        const char *s;

        if (from_image) {
          /* D64 filenames are already PETSCII — send as-is */
          snprintf(c64name, sizeof(c64name), "%s", de->name);
        } else {
          /* PC filesystem: convert .prg→,p etc. */
          pc_to_c64_filename(de->name, c64name, sizeof(c64name));
        }

        dbg("MP-SEND: announcing '%s' as C64 name '%s' (%d/%d) from_image=%d\n",
            de->name, c64name, filecount + 1, total_tagged, from_image);
        xfer_send_byte(0x09);
        /* Strip control/reserved bytes from the announced name: 0x09 is the
         * field delimiter and 0x0D/0x0A/0x00 terminate the name on the BBS
         * side, so any such byte in a (legal) PETSCII D64 filename would
         * truncate or split the name. Mirror the receive-side sanitizer,
         * which drops every byte < 0x20. Printable/high PETSCII is preserved. */
        s = c64name;
        while (*s) {
          unsigned char ch = (unsigned char)*s++;
          if (ch < 0x20) {
            continue;
          }
          xfer_send_byte(ch);
        }
      }
      xfer_send_byte('\r');
      dbg(" MP-SEND: file size=%d, calling punter_send_no_presignal\n", xfer_file_size);

      /* Punter send without the GOO pre-signal — the BBS already started
       * initrecv2 after reading our filename announcement (bbs.bas line 3677) */
      send_result = punter_send_no_presignal();
      dbg(" MP-SEND: punter_send_no_presignal returned %d\n", send_result);

      fclose(xfer_sendfile);
      if (send_result) {
        filecount++;
      } else {
        /* The BBS state is unknown after a failed Punter session; stop the
         * batch instead of announcing the next file into it. */
        aborted = 1;
      }

      /* Wait for BBS to finish writing file and return to the
       * get#5 loop (bbs.bas line 3610-3620). Drain any stray
       * bytes from the Punter close-out before sending the
       * next file announcement.
       * Use multiple drain passes with delays — the BBS may still
       * be writing to disk and sending status bytes. */
      {
        int drain_pass;
        for (drain_pass = 0; drain_pass < 3; drain_pass++) {
          timer_delay(1500);
          while (net_receive_raw() >= 0) { /* drain */ }
        }
        dbg("MP-SEND: drain complete after file %d\n", filecount);
      }
    } else {
      dbg("MP-SEND: fopen FAILED for '%s'\n", name);
      snprintf(msg, sizeof(msg), "Couldn't open %s", de->name);
      menu_draw_message(msg);
      menu_show();
      gfx_vbl();
    }

    if (deletetmp) {
      xfer_cleanup_temp_upload();
    }

    if (aborted) {
      break;
    }
    de = de->next;
  }

  /* Send batch end: 0x09 + 0x04 (EOT) */
  dbg("MP-SEND: sending batch end (0x09 0x04), files sent=%d\n", filecount);
  xfer_send_byte(0x09);
  xfer_send_byte(0x04);

  /* Wait for BBS to process the batch end, then drain any remaining
   * protocol bytes so they don't appear as garbage on the terminal */
  timer_delay(2000);
  while (net_receive_raw() >= 0) { /* drain */ }

  if (filecount < total_tagged) {
    snprintf(msg, sizeof(msg), "Multi Punter: %d of %d file%s sent%s",
             filecount, total_tagged, total_tagged == 1 ? "" : "s",
             xfer_cancel ? " (cancelled)" : " (a transfer failed)");
    menu_draw_message_timed(msg, 5000);
  } else if (filecount > 0) {
    snprintf(msg, sizeof(msg), "Sent %d Multi Punter file%s",
             filecount, filecount == 1 ? "" : "s");
    menu_draw_message_timed(msg, 5000);
  } else {
    menu_draw_message_timed("Multi Punter: no files sent", 5000);
  }
}



/* ZMODEM batch upload of every tagged file (or the selected one). Each file
 * is opened on xfer_sendfile like the other protocols; files inside a disk
 * image are extracted to a temp file first. */
void xfer_send_zmodem(FileSelector *fs) {
  DirEntry *de;
  char name[1088];
  char msg[256];
  int filecount = 0, total_tagged = fs->numtagged, ok = 1;
  int from_image = path_is_disk_image(cfg_xferdir);

  xfer_cancel = 0;
  xfer_confirm_pending = 0;
  xfer_starttime = timer_get_ticks();
  xfer_protocol = PROT_ZMODEM;
  xfer_direction = DIR_SEND;
  menu_draw_xfer_progress("ZMODEM", xfer_direction, xfer_protocol);
  menu_show();
  gfx_vbl();

  if (total_tagged == 0 && fs->selectedfile && fs->selectedfile->name && fs->selectedfile->type != T_DIR) {
    fs->selectedfile->tagged = 1;
    total_tagged = 1;
  }

  if (!zmodem_send_begin()) {
    menu_draw_message_timed("ZMODEM: receiver did not answer", 5000);
    xfer_log_result("send", NULL, 0, timer_get_ticks() - xfer_starttime, "FAILED (no ZRINIT)");
    return;
  }

  for (de = fs->dir->firstentry; de && !xfer_cancel && ok; de = de->next) {
    int deletetmp = 0;
    unsigned int t0;

    if (!de->tagged || !de->name || de->type == T_DIR) continue;

    if (from_image) {
      if (xfer_create_temp_upload() && xfer_copy_from_image(cfg_xferdir, de->name, de->rawname, xfer_tempulname)) {
        snprintf(name, sizeof(name), "%s", xfer_tempulname);
        deletetmp = 1;
      } else {
        xfer_cleanup_temp_upload();
        snprintf(msg, sizeof(msg), "Couldn't open %s", de->name);
        menu_draw_message(msg);
        menu_show();
        gfx_vbl();
        continue;
      }
    } else {
      snprintf(name, sizeof(name), "%s%c%s", cfg_xferdir,
#ifdef WINDOWS
        '\\',
#else
        '/',
#endif
        de->name);
    }

    if ((xfer_sendfile = fopen(name, "rb")) == NULL) {
      if (deletetmp) xfer_cleanup_temp_upload();
      continue;
    }
    fseek(xfer_sendfile, 0, SEEK_END);
    xfer_file_size = (int)ftell(xfer_sendfile);
    fseek(xfer_sendfile, 0, SEEK_SET);
    xfer_saved_bytes = 0;
    t0 = timer_get_ticks();
    menu_draw_xfer_progress(de->name, xfer_direction, xfer_protocol);
    menu_show();
    gfx_vbl();

    {
      /* announce PC names with their C64 suffix converted (file.prg -> file.prg is
       * fine for ZMODEM receivers; keep the host name as is) */
      int r = zmodem_send_file(de->name);
      fclose(xfer_sendfile);
      xfer_sendfile = NULL;
      xfer_log_result("send", de->name, r ? (long)xfer_file_size : (long)xfer_saved_bytes,
                      timer_get_ticks() - t0, r ? "ok" : (xfer_cancel ? "cancelled" : "FAILED"));
      if (r) {
        filecount++;
      } else {
        ok = 0;
      }
    }
    if (deletetmp) xfer_cleanup_temp_upload();
  }

  if (ok) {
    zmodem_send_end();
  }
  if (filecount == total_tagged && filecount > 0) {
    snprintf(msg, sizeof(msg), "Sent %d ZMODEM file%s", filecount, filecount == 1 ? "" : "s");
  } else {
    snprintf(msg, sizeof(msg), "ZMODEM: %d of %d file%s sent%s", filecount, total_tagged,
             total_tagged == 1 ? "" : "s", xfer_cancel ? " (cancelled)" : "");
  }
  menu_draw_message_timed(msg, 5000);
}
