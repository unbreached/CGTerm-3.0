#include <stdio.h>
#include <string.h>
#include "net.h"
#include "clipboard.h"

#ifdef WINDOWS
#include <windows.h>
#endif

/* Pasted text is queued here and drained one byte per main-loop iteration so
 * it is paced like typed input instead of flooding the BBS in a single burst
 * (a C64 BBS at 1200-2400 baud drops characters when hundreds arrive at once).
 * The main loop pulls bytes via clipboard_paste_byte(). */
#define PASTE_MAX 8192
static unsigned char paste_buf[PASTE_MAX];
static int paste_len = 0;
static int paste_pos = 0;

/* Translate one source char to a C64 byte. Collapses CR/LF (and CRLF/ LFCR
 * pairs) to a single CR and swaps letter case (C64 PETSCII convention),
 * matching what the keyboard handler sends. Returns -1 to emit nothing. */
static int paste_translate(char c) {
  if (c == '\n' || c == '\r') return '\r';
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 'a' && c <= 'z') return c - 32;
  if ((unsigned char)c < 32 && c != '\t') return -1;  /* drop other controls */
  return (unsigned char)c;
}

static void paste_push(const char *text, int n) {
  int i;
  for (i = 0; i < n && paste_len < PASTE_MAX; i++) {
    int tc;
    /* swallow the partner of a CRLF / LFCR pair so it becomes one CR */
    if ((text[i] == '\r' && i + 1 < n && text[i + 1] == '\n') ||
        (text[i] == '\n' && i + 1 < n && text[i + 1] == '\r')) {
      paste_buf[paste_len++] = '\r';
      i++;
      continue;
    }
    tc = paste_translate(text[i]);
    if (tc >= 0) paste_buf[paste_len++] = (unsigned char)tc;
  }
}

void clipboard_paste(void) {
  /* (re)fill the queue — replaces any partially-drained previous paste */
  paste_len = 0;
  paste_pos = 0;

#ifdef WINDOWS
  if (!OpenClipboard(NULL)) return;
  {
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) {
      char *text = (char *)GlobalLock(h);
      if (text) {
        paste_push(text, (int)strlen(text));
        GlobalUnlock(h);
      }
    }
  }
  CloseClipboard();
#else
  {
    FILE *fp = NULL;
    char buf[4096];
    int len;

#ifdef __APPLE__
    fp = popen("pbpaste", "r");
#else
    fp = popen("xclip -selection clipboard -o 2>/dev/null || xsel --clipboard -o 2>/dev/null", "r");
#endif
    if (!fp) return;

    while ((len = (int)fread(buf, 1, sizeof(buf), fp)) > 0) {
      paste_push(buf, len);
    }
    pclose(fp);
  }
#endif
}

/* Next queued paste byte, or -1 if the queue is empty/drained. */
int clipboard_paste_byte(void) {
  if (paste_pos < paste_len) {
    return paste_buf[paste_pos++];
  }
  return -1;
}

int clipboard_paste_pending(void) {
  return paste_pos < paste_len;
}

void clipboard_paste_clear(void) {
  paste_len = 0;
  paste_pos = 0;
}
