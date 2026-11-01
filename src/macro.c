#include "config.h"
#include "clipboard.h"
#include "macro.h"

#define MACROSIZE 4096


int macro_rec = 0;
int macro_len = 0;
int macro_maxlen = MACROSIZE;
int macro_play = 0;
int macro_ctr = 0;
unsigned char macrobuf_key[MACROSIZE];
unsigned char macrobuf_shift[MACROSIZE];
unsigned char macrobuf_ctrl[MACROSIZE];
unsigned char macrobuf_cbm[MACROSIZE];


/* Send text macro slot n (0..9) to the board, paced like a paste. The stored
 * text may use backslash escapes: \r or \n = RETURN, \t = TAB/RUN-STOP, \\ = a
 * literal backslash. Everything else goes through as typed (issue #8). */
void macro_send_slot(int n) {
  char out[512];
  const char *s;
  int o = 0;

  if (n < 0 || n > 9) return;
  s = cfg_macros[n];
  if (!s[0]) return;

  for (; *s && o < (int)sizeof(out) - 1; s++) {
    if (*s == '\\' && s[1]) {
      s++;
      switch (*s) {
        case 'r': case 'n': out[o++] = '\r'; break;
        case 't':           out[o++] = '\t'; break;
        default:            out[o++] = *s;   break;   /* \\ -> \, \x -> x */
      }
    } else {
      out[o++] = *s;
    }
  }
  out[o] = 0;
  clipboard_queue_text(out);
}
