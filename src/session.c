#include <stdio.h>
#include <string.h>
#include <time.h>
#include "session.h"
#include "config.h"
#include "gfx.h"
#include "net.h"
#include "timer.h"
#include <SDL.h>
#include "keyboard.h"
#include "macro.h"
#include "xfer.h"
#include "login.h"

static FILE *capture = NULL;
static char capture_name[1024];
static int capture_newline = 1;
static int capture_last = 0;

/* Every line of the capture starts with a wall-clock stamp; RETURN ends a
 * line. Other PETSCII/ANSI bytes are written untouched so colour codes and
 * escape sequences survive for later replay or conversion. */
static void capture_write_byte(int byte) {
  if (!capture) return;
  if (capture_newline) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    fprintf(capture, "[%02d:%02d:%02d] ", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    capture_newline = 0;
  }
  if (cfg_termmode == 1 && byte == 0x0A && capture_last == 0x0D) {
    capture_last = byte;          /* the LF of a CR LF pair: line already ended */
    return;
  }
  if (fputc(byte, capture) == EOF) {
    return;
  }
  capture_last = byte;
  if (byte == 0x0D || (cfg_termmode == 0 && byte == 0x8D) || (cfg_termmode == 1 && byte == 0x0A)) {
    fputc('\n', capture);
    capture_newline = 1;
  }
}

int session_capture_open(const char *path) {
  if (capture) {
    return 1;
  }
  if (path && path[0]) {
    snprintf(capture_name, sizeof(capture_name), "%s", path);
  } else {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm_info);
#ifdef WINDOWS
    snprintf(capture_name, sizeof(capture_name), "%s\\cgterm-capture-%s.txt", cfg_dldir, stamp);
#else
    snprintf(capture_name, sizeof(capture_name), "%s/cgterm-capture-%s.txt", cfg_dldir, stamp);
#endif
  }
  /* append, never truncate: a logfile= capture must survive restarts */
  if ((capture = fopen(capture_name, "ab")) == NULL) {
    printf("Couldn't open capture file %s\n", capture_name);
    return 0;
  }
  {
    time_t now = time(NULL);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(capture, "=== CGTerm capture %s %s ===\n", stamp,
            cfg_host ? cfg_host : "(not connected)");
  }
  capture_newline = 1;
  return 1;
}

void session_capture_close(void) {
  if (capture) {
    fclose(capture);
    capture = NULL;
  }
}

int session_capture_active(void) {
  return capture != NULL;
}

const char *session_capture_path(void) {
  return capture_name;
}

void session_capture_byte(int byte) {
  capture_write_byte(byte);
}

/* ---- status row ---- */

static char status_override[81];
static unsigned int status_override_until = 0;

void session_status_message(const char *text, unsigned int ms) {
  snprintf(status_override, sizeof(status_override), "%s", text ? text : "");
  status_override_until = timer_get_ticks() + ms;
}

void session_status_tick(void) {
  char line[128];
  char left[64];
  char right[40];
  time_t now;
  struct tm *tm_info;
  int width;

  if (!gfx_status_enabled()) {
    return;
  }
  if (status_override[0] && timer_get_ticks() < status_override_until) {
    gfx_status_set(status_override, 1, 0);
    return;
  }
  status_override[0] = 0;

  if (net_connected()) {
    if (cfg_connect_name[0]) {
      snprintf(left, sizeof(left), "%.30s", cfg_connect_name);
    } else {
      snprintf(left, sizeof(left), "%.24s:%d", cfg_host ? cfg_host : "?", cfg_port);
    }
  } else if (cfg_reconnect && cfg_host && cfg_nextreconnect) {
    unsigned int t = timer_get_ticks();
    unsigned int left_ms = cfg_nextreconnect > t ? cfg_nextreconnect - t : 0;
    snprintf(left, sizeof(left), "redial in %us", (left_ms + 999) / 1000);
  } else {
    snprintf(left, sizeof(left), "offline");
  }

  now = time(NULL);
  tm_info = localtime(&now);
  snprintf(right, sizeof(right), "%s%s%s%s %02d:%02d",
           cfg_termmode == 1 ? "ansi" : "pet",
           cfg_columns == 80 ? "80" : "40",
           session_capture_active() ? " cap" : "",
           macro_rec ? " rec" : (login_active() ? " login" : ""),
           tm_info->tm_hour, tm_info->tm_min);
  width = cfg_columns - (int)strlen(right) - 2;
  if (width < 1) width = 1;
  snprintf(line, sizeof(line), " %-*.*s%s ", width, width, left, right);
  gfx_status_set(line, net_connected() ? 13 : 12, 0);
}

/* ---- ZMODEM auto-start ----
 * A ZMODEM sender announces itself with the ZRQINIT hex header
 * "**" ZDLE 'B' "00" ... ; seeing that in the terminal stream means the
 * board has started sending and we should answer with a receive. */
int session_zmodem_sentinel(int c) {
  static const unsigned char pat[6] = {'*', '*', 0x18, 'B', '0', '0'};
  static int pos = 0;

  if (c < 0) return 0;
  if ((unsigned char)c == pat[pos]) {
    if (++pos == 6) {
      pos = 0;
      return 1;
    }
  } else {
    /* "***" keeps the two-star prefix alive; anything else restarts */
    pos = ((unsigned char)c == '*') ? (pos >= 2 ? 2 : 1) : 0;
  }
  return 0;
}
