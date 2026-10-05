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

/* CR/LF pairing state carried across fread() chunks, so a "\r" that ends
 * one 4096-byte chunk and the "\n" that starts the next collapse to one CR
 * instead of producing a blank line in the message being pasted. */
static int last_was_cr = 0;
static int last_was_lf = 0;

/* UTF-8 decoder state: pending code point and bytes still expected. */
static unsigned int utf8_cp = 0;
static int utf8_need = 0;

/* Translate one ASCII char to a C64 byte: swap letter case (PETSCII
 * convention), keep printable ASCII and TAB, drop other controls.
 * Returns -1 to emit nothing. */
static int paste_translate_ascii(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 'a' && c <= 'z') return c - 32;
  if (c < 32 && c != '\t') return -1;
  if (c >= 127) return -1;
  return c;
}

/* Reduce a Unicode code point to something a PETSCII board can show:
 * accented Latin letters lose their accent, typographic quotes and dashes
 * become ASCII, anything else becomes '?'. Returns the ASCII byte. */
static unsigned char unicode_to_ascii(unsigned int cp) {
  static const struct { unsigned int from, to; unsigned char c; } ranges[] = {
    {0x00C0, 0x00C5, 'A'}, {0x00C8, 0x00CB, 'E'}, {0x00CC, 0x00CF, 'I'},
    {0x00D2, 0x00D6, 'O'}, {0x00D9, 0x00DC, 'U'}, {0x00E0, 0x00E5, 'a'},
    {0x00E8, 0x00EB, 'e'}, {0x00EC, 0x00EF, 'i'}, {0x00F2, 0x00F6, 'o'},
    {0x00F9, 0x00FC, 'u'}, {0x00C7, 0x00C7, 'C'}, {0x00E7, 0x00E7, 'c'},
    {0x00D1, 0x00D1, 'N'}, {0x00F1, 0x00F1, 'n'}, {0x00DF, 0x00DF, 's'},
    {0x00D8, 0x00D8, 'O'}, {0x00F8, 0x00F8, 'o'}, {0x00C6, 0x00C6, 'A'},
    {0x00E6, 0x00E6, 'a'}, {0x00DD, 0x00DD, 'Y'}, {0x00FD, 0x00FF, 'y'},
    {0x2018, 0x201B, '\''}, {0x201C, 0x201F, '"'}, {0x2010, 0x2015, '-'},
    {0x2026, 0x2026, '.'}, {0x00A0, 0x00A0, ' '}, {0x00A3, 0x00A3, '#'},
  };
  size_t i;
  if (cp < 128) return (unsigned char)cp;
  for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
    if (cp >= ranges[i].from && cp <= ranges[i].to) return ranges[i].c;
  }
  return '?';
}

static void paste_emit(unsigned char ascii) {
  int tc = paste_translate_ascii(ascii);
  if (tc >= 0 && paste_len < PASTE_MAX) paste_buf[paste_len++] = (unsigned char)tc;
}

static void paste_push(const char *text, int n) {
  int i;
  if (paste_pos >= paste_len) {
    paste_len = paste_pos = 0;          /* everything queued so far was sent */
  } else if (paste_pos > 0) {
    memmove(paste_buf, paste_buf + paste_pos, paste_len - paste_pos);
    paste_len -= paste_pos;
    paste_pos = 0;
  }
  for (i = 0; i < n && paste_len < PASTE_MAX; i++) {
    unsigned char c = (unsigned char)text[i];

    /* line endings: CRLF, LFCR, CR and LF all become a single RETURN */
    if (c == '\r' || c == '\n') {
      if ((c == '\n' && last_was_cr) || (c == '\r' && last_was_lf)) {
        last_was_cr = last_was_lf = 0;   /* second half of a pair: swallow */
        continue;
      }
      paste_buf[paste_len++] = '\r';
      last_was_cr = (c == '\r');
      last_was_lf = (c == '\n');
      continue;
    }
    last_was_cr = last_was_lf = 0;

    /* UTF-8 from pbpaste / xclip: decode instead of forwarding the raw
     * bytes, which arrive at a PETSCII board as colour and graphics codes */
    if (utf8_need > 0) {
      if ((c & 0xC0) == 0x80) {
        utf8_cp = (utf8_cp << 6) | (c & 0x3F);
        if (--utf8_need == 0) paste_emit(unicode_to_ascii(utf8_cp));
        continue;
      }
      utf8_need = 0;   /* malformed: fall through and treat c on its own */
    }
    if (c >= 0xC2 && c <= 0xDF) { utf8_cp = c & 0x1F; utf8_need = 1; continue; }
    if (c >= 0xE0 && c <= 0xEF) { utf8_cp = c & 0x0F; utf8_need = 2; continue; }
    if (c >= 0xF0 && c <= 0xF4) { utf8_cp = c & 0x07; utf8_need = 3; continue; }
    if (c >= 0x80) { paste_emit(unicode_to_ascii(c)); continue; }   /* Latin-1 */
    paste_emit(c);
  }
}

static void paste_reset_state(void) {
  last_was_cr = last_was_lf = 0;
  utf8_need = 0;
  utf8_cp = 0;
}

void clipboard_queue_text(const char *text) {
  if (!text) return;
  paste_reset_state();
  paste_push(text, (int)strlen(text));
}

void clipboard_paste(void) {
  /* (re)fill the queue — replaces any partially-drained previous paste */
  paste_len = 0;
  paste_pos = 0;
  paste_reset_state();

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
    fp = popen("xclip -selection clipboard -o 2>/dev/null || xsel --clipboard -o 2>/dev/null || wl-paste 2>/dev/null", "r");
#endif
    if (!fp) return;

    while ((len = (int)fread(buf, 1, sizeof(buf), fp)) > 0) {
      paste_push(buf, len);
    }
    pclose(fp);
  }
#endif
}

int clipboard_copy_text(const char *text) {
  if (!text) return 0;
#ifdef WINDOWS
  {
    size_t len = strlen(text) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, len);
    char *p;
    if (!h) return 0;
    p = (char *)GlobalLock(h);
    if (!p) { GlobalFree(h); return 0; }
    memcpy(p, text, len);
    GlobalUnlock(h);
    if (!OpenClipboard(NULL)) { GlobalFree(h); return 0; }
    EmptyClipboard();
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
    return 1;
  }
#else
  {
    FILE *fp;
#ifdef __APPLE__
    fp = popen("pbcopy", "w");
#else
    fp = popen("xclip -selection clipboard -i 2>/dev/null || xsel --clipboard -i 2>/dev/null || wl-copy 2>/dev/null", "w");
#endif
    if (!fp) return 0;
    fputs(text, fp);
    return pclose(fp) == 0;
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
  paste_reset_state();
}
