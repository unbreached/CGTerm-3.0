/* ANSI emulation regression harness: drives ansi_out() against the real
 * gfx.c screen model under SDL's dummy video driver and checks the
 * character / colour / background planes. No window, no network.
 *
 * Build (from the repo root):
 *   cc -O1 -g -Wall -Isrc $(sdl-config --cflags) -DPREFIX=\"/usr/local\" \
 *      -o /tmp/test_ansi tests/test_ansi.c src/ansi.c src/gfx.c src/font.c \
 *      src/cp437font.c src/config.c src/paths.c src/menu.c src/kernal.c \
 *      $(sdl-config --libs) -lm
 *   /tmp/test_ansi
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "gfx.h"
#include "config.h"
#include "kernal.h"
#include "ansi.h"
#include "sound.h"
#include "keyboard.h"

/* ---- stubs for the net / sound / ui symbols gfx.c, kernal.c and ansi.c reference ---- */
int sound_bell = 0;
static int bells = 0;
void sound_play_sample(int sample) { (void)sample; bells++; }
static char sent[256];
void net_send_string(const unsigned char *s) {
  size_t n = strlen((const char *)s);
  if (n >= sizeof(sent)) n = sizeof(sent) - 1;
  memcpy(sent, s, n);
  sent[n] = 0;
}
void net_send(unsigned char c) { (void)c; }
int net_connected(void) { return 0; }
Focus kbd_focus;
int kbd_getkey(void) { return 0; }
void kbd_add_focus(Focus f, void (*h)(SDL_keysym *)) { (void)f; (void)h; }
int kbd_reload(char *keyboardcfg) { (void)keyboardcfg; return 0; }
void ui_init(void) {}
void ui_inputcall(int w, char *t, char *x, void (*cb)(char *), Focus f) { (void)w; (void)t; (void)x; (void)cb; (void)f; }
void ui_inputcall_on_cancel(void (*cb)(void)) { (void)cb; }
void ui_menu(void) {}
void ui_pageup(void) {}
void ui_pagedown(void) {}
void ui_metakey(SDL_keysym *k) { (void)k; }
unsigned char keytable[SDLK_LAST][5];
int macro_rec = 0, macro_play = 0, macro_len = 0, macro_ctr = 0, macro_maxlen = 0;
unsigned char macrobuf_key[1], macrobuf_shift[1], macrobuf_ctrl[1], macrobuf_cbm[1];
void clipboard_paste(void) {}
void clipboard_paste_clear(void) {}

static int fails = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s  (line %d)\n", msg, __LINE__); fails++; } } while (0)

static void feed(const char *s) {
  while (*s) ansi_out((unsigned char)*s++);
}

static unsigned char cell(int x, int y) { return gfx_0400[y * cfg_columns + x]; }
static unsigned char col(int x, int y)  { return gfx_d800[y * cfg_columns + x]; }
static unsigned char bg(int x, int y)   { return gfx_bg[y * cfg_columns + x]; }

/* Row y as a NUL-terminated string with trailing blanks removed */
static const char *row(int y) {
  static char buf[128];
  int n = cfg_columns;
  memcpy(buf, gfx_0400 + y * cfg_columns, cfg_columns);
  while (n > 0 && buf[n - 1] == ' ') n--;
  buf[n] = 0;
  return buf;
}

static int row_all(int y, unsigned char ch) {
  int x;
  for (x = 0; x < cfg_columns; x++) if (cell(x, y) != ch) return 0;
  return 1;
}

static int row_bg_all(int y, unsigned char c) {
  int x;
  for (x = 0; x < cfg_columns; x++) if (bg(x, y) != c) return 0;
  return 1;
}

/* Fresh screen: full reset, then fill rows 0..rows-1 with "R<n>" markers */
static void fresh(void) {
  feed("\033c");
}

static void fill_rows(int rows) {
  int y;
  char buf[32];
  for (y = 0; y < rows; y++) {
    snprintf(buf, sizeof(buf), "\033[%d;1HR%d", y + 1, y);
    feed(buf);
  }
  feed("\033[1;1H");
}

int main(int argc, char *argv[]) {
  (void)argc;

  setenv("SDL_VIDEODRIVER", "dummy", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  /* assets/ relative to the repo root (the test is run from there); the
   * exe-relative lookup in paths.c would otherwise look next to /tmp. */
  setenv("CGTERM_ASSET_DIR", "assets", 0);

  cfg_init(argv[0]);
  cfg_columns = 80;
  cfg_rows = 25;
  cfg_zoom = 2;
  cfg_termmode = 1;
  if (gfx_init(0, "test_ansi")) {
    printf("gfx_init failed (SDL dummy driver / assets missing?)\n");
    return 2;
  }
  ansi_init();

  printf("scroll region (DECSTBM)\n");
  fresh();
  fill_rows(10);
  feed("\033[1;5r");                 /* region rows 0-4, cursor homes */
  CHECK(gfx_cursx == 0 && gfx_cursy == 0, "DECSTBM homes the cursor");
  feed("\033[5;1H\n");               /* LF at region bottom */
  CHECK(!strcmp(row(0), "R1"), "LF at region bottom scrolls row 1 into row 0");
  CHECK(!strcmp(row(3), "R4"), "row 4 moved up to row 3");
  CHECK(row_all(4, ' '), "vacated region bottom row is blank");
  CHECK(!strcmp(row(5), "R5") && !strcmp(row(9), "R9"), "rows outside the region untouched");
  CHECK(gfx_cursy == 4, "cursor stays on the region bottom row");
  feed("\n\n\n\n");
  CHECK(row_all(0, ' ') && row_all(4, ' ') && !strcmp(row(5), "R5"), "five LFs clear the region only");

  fresh();
  fill_rows(10);
  feed("\033[3;6r");                 /* rows 2-5 */
  feed("\033[3;1H\033M");            /* RI at region top */
  CHECK(row_all(2, ' ') && !strcmp(row(3), "R2") && !strcmp(row(5), "R4"), "RI at region top scrolls region down");
  CHECK(!strcmp(row(1), "R1") && !strcmp(row(6), "R6"), "RI leaves rows outside region alone");
  feed("\033[2S");
  CHECK(!strcmp(row(2), "R3") && row_all(4, ' ') && row_all(5, ' ') && !strcmp(row(6), "R6"), "ESC[2S scrolls region up by 2");
  feed("\033[1T");
  CHECK(row_all(2, ' ') && !strcmp(row(3), "R3"), "ESC[1T scrolls region down by 1");

  fresh();
  fill_rows(10);
  feed("\033[3;6r\033[4;1H\033[L");  /* IL at row 3 inside region 2-5 */
  CHECK(!strcmp(row(2), "R2") && row_all(3, ' ') && !strcmp(row(4), "R3") && !strcmp(row(5), "R4") && !strcmp(row(6), "R6"), "IL inserts inside the region, R5 falls off the region bottom");
  feed("\033[M");
  CHECK(!strcmp(row(3), "R3") && !strcmp(row(4), "R4") && row_all(5, ' '), "DL deletes inside the region");
  feed("\033[8;1H\033[L");
  CHECK(!strcmp(row(7), "R7") && !strcmp(row(8), "R8"), "IL outside the region is a no-op");
  feed("\033[7;1H\n");
  CHECK(gfx_cursy == 7, "LF below the region moves down without scrolling");
  feed("\033[r");
  feed("\033[25;1H\033[1;1HX\033[25;1H\n");
  CHECK(cell(0, 0) == ' ' || gfx_cursy == 24, "ESC[r restores full-screen scrolling");

  printf("auto-wrap / pending wrap (DECAWM)\n");
  fresh();
  {
    int i;
    for (i = 0; i < 80; i++) ansi_out('x');
  }
  CHECK(gfx_cursx == 79 && gfx_cursy == 0, "80th glyph leaves the cursor in column 80 (pending wrap)");
  feed("\r\n");
  CHECK(gfx_cursx == 0 && gfx_cursy == 1, "CR LF after a full line goes to row 1, not row 2");
  feed("z");
  CHECK(cell(0, 1) == 'z', "next line starts on row 1 (no blank row)");
  fresh();
  {
    int i;
    for (i = 0; i < 80; i++) ansi_out('x');
  }
  feed("y");
  CHECK(cell(0, 1) == 'y' && gfx_cursx == 1 && gfx_cursy == 1, "81st glyph wraps to row 1 column 1");
  CHECK(cell(79, 0) == 'x', "column 80 keeps the 80th glyph");
  fresh();
  feed("\033[?7l");
  {
    int i;
    for (i = 0; i < 85; i++) ansi_out('a' + i % 26);
  }
  CHECK(gfx_cursy == 0 && gfx_cursx == 79 && cell(79, 0) == 'a' + 84 % 26, "DECAWM off: cursor sticks in the last column and overwrites");
  feed("\033[?7h");
  fresh();
  feed("\033[1;80Hq\033[1;80Hw\033[1;80H\033[K");
  CHECK(cell(79, 0) == ' ' && gfx_cursy == 0, "cursor move clears the pending wrap");
  fresh();
  feed("\033[r\033[25;80Hab");
  CHECK(cell(79, 23) == 'a' && cell(0, 24) == 'b' && gfx_cursy == 24, "wrap at the last screen row scrolls the screen");

  printf("DECSC / DECRC, SCOSC / SCORC\n");
  fresh();
  feed("\033[1;31m\033[44m\033[5;10H\0337\033[0m\033[1;1H\0338");
  CHECK(gfx_cursx == 9 && gfx_cursy == 4, "ESC 8 restores the cursor position");
  feed("q");
  CHECK(col(9, 4) == COLOR_LTRED && bg(9, 4) == COLOR_BLUE, "ESC 8 restores bold red on blue");
  feed("\033[0m\033[3;3H\033[s\033[10;10H\033[u");
  CHECK(gfx_cursx == 2 && gfx_cursy == 2, "ESC[s / ESC[u restore the position");

  printf("insert / delete / erase characters\n");
  fresh();
  feed("abcdef\033[1;2H\033[3@");
  CHECK(!strcmp(row(0), "a   bcdef"), "ESC[3@ shifts the row right by 3");
  CHECK(gfx_cursx == 1, "ICH does not move the cursor");
  feed("\033[2P");
  CHECK(!strcmp(row(0), "a bcdef"), "ESC[2P deletes two cells");
  feed("\033[1;3H\033[2X");
  CHECK(!strcmp(row(0), "a   def"), "ESC[2X erases two cells without shifting");
  fresh();
  feed("\033[1;78Habcdef");
  CHECK(cell(79, 0) == 'c' && gfx_cursy == 1 && cell(2, 1) == 'f', "writing past column 80 wraps mid-string");

  printf("cursor addressing\n");
  fresh();
  feed("\033[5;5H\033[E");
  CHECK(gfx_cursx == 0 && gfx_cursy == 5, "CNL moves down to column 0");
  feed("\033[5;5H\033[2F");
  CHECK(gfx_cursx == 0 && gfx_cursy == 2, "CPL moves up to column 0");
  feed("\033[10G");
  CHECK(gfx_cursx == 9, "CHA sets the column");
  feed("\033[7d");
  CHECK(gfx_cursy == 6 && gfx_cursx == 9, "VPA sets the row");
  feed("\033[20`");
  CHECK(gfx_cursx == 19, "HPA sets the column");
  feed("\033[255;255H");
  CHECK(gfx_cursx == 79 && gfx_cursy == 24, "CUP clamps to the screen");
  feed("\033[200A\033[200D");
  CHECK(gfx_cursx == 0 && gfx_cursy == 0, "CUU/CUB clamp at the margins");
  feed("\033[5;10r\033[7;1H\033[20A");
  CHECK(gfx_cursy == 4, "CUU inside the region stops at the region top");
  feed("\033[20B");
  CHECK(gfx_cursy == 9, "CUD inside the region stops at the region bottom");
  feed("\033[r");

  printf("SGR / iCE colours\n");
  fresh();
  feed("\033[5;41mA");
  CHECK(bg(0, 0) == COLOR_LTRED, "ESC[5;41m gives a bright red background");
  feed("\033[25mB");
  CHECK(bg(1, 0) == COLOR_RED, "ESC[25m returns to normal red");
  feed("\033[41;5mC");
  CHECK(bg(2, 0) == COLOR_LTRED, "ESC[41;5m is bright too (order independent)");
  feed("\033[0;1;34mD");
  CHECK(col(3, 0) == COLOR_LTBLUE && bg(3, 0) == 0, "bold blue foreground, reset background");
  feed("\033[22mE");
  CHECK(col(4, 0) == COLOR_BLUE, "ESC[22m un-bolds");
  feed("\033[93;104mF");
  CHECK(col(5, 0) == COLOR_YELLOW && bg(5, 0) == COLOR_LTBLUE, "90-97 / 100-107 bright colours");
  feed("\033[39;49mG");
  CHECK(col(6, 0) == COLOR_LTGRAY && bg(6, 0) == 0, "39/49 restore defaults");
  feed("\033[2;3;4;8;9;31mH");
  CHECK(col(7, 0) == COLOR_RED && cell(7, 0) == 'H', "2/3/4/8/9 are ignored, 31 still applies");
  feed("\033[0;7;32mI");
  CHECK(col(8, 0) == 0 && bg(8, 0) == COLOR_GREEN, "reverse swaps fg/bg");
  feed("\033[27mJ");
  CHECK(col(9, 0) == COLOR_GREEN && bg(9, 0) == 0, "reverse off");
  feed("\033[0;38;5;10;48;5;4mK");
  CHECK(col(10, 0) == COLOR_LTGREEN && bg(10, 0) == COLOR_BLUE, "38;5 / 48;5");
  feed("\033[0;38;2;255;80;80mL");
  CHECK(col(11, 0) == COLOR_LTRED, "38;2 truecolour maps to light red");
  feed("\033[mM");
  CHECK(col(12, 0) == COLOR_LTGRAY && bg(12, 0) == 0, "ESC[m resets");

  printf("erase with BCE\n");
  fresh();
  feed("\033[1;1Hhello world\033[1;6H\033[41m\033[K");
  CHECK(!strcmp(row(0), "hello"), "ESC[K clears to end of line");
  CHECK(bg(5, 0) == COLOR_RED && bg(79, 0) == COLOR_RED && bg(0, 0) == 0, "ESC[K fills with the red background (BCE)");
  CHECK(gfx_cursx == 5, "EL does not move the cursor");
  feed("\033[1K");
  CHECK(bg(0, 0) == COLOR_RED && cell(5, 0) == ' ', "ESC[1K clears to the cursor inclusive");
  feed("\033[0m\033[3;1HX\033[2;1H\033[44m\033[J");
  CHECK(cell(0, 2) == ' ' && bg(0, 2) == COLOR_BLUE && bg(79, 24) == COLOR_BLUE && bg(0, 0) == COLOR_RED, "ESC[J clears below with blue, keeps rows above");
  feed("\033[42m\033[2J");
  CHECK(row_bg_all(0, COLOR_GREEN) && row_bg_all(24, COLOR_GREEN) && gfx_cursx == 0 && gfx_cursy == 0, "ESC[2J clears all with green and homes");
  feed("\033[0m\033[1;1H\033[43m\033[2;1H\033[1J");
  CHECK(bg(0, 0) == COLOR_BROWN && bg(0, 1) == COLOR_BROWN && bg(1, 1) == COLOR_GREEN, "ESC[1J clears to the cursor inclusive");
  feed("\033[0m\033[44m\033[25;1H\n");
  CHECK(row_bg_all(24, COLOR_BLUE), "scrolling fills the new row with the current background");
  feed("\033[0m\033[2J");

  printf("ESC sequences and parser robustness\n");
  fresh();
  feed("\033[1;1Habc\033Ex");
  CHECK(cell(0, 1) == 'x' && gfx_cursy == 1, "NEL moves to the start of the next row");
  feed("\033D");
  CHECK(gfx_cursy == 2 && gfx_cursx == 1, "IND moves down keeping the column");
  feed("\033=\033>Q");
  CHECK(cell(1, 2) == 'Q', "ESC = / ESC > consume one byte only");
  feed("\033#8W");
  CHECK(cell(2, 2) == 'W', "ESC # 8 consumes exactly its final byte");
  feed("\033(0qx\033(Bq");
  CHECK(cell(3, 2) == 0xC4 && cell(4, 2) == 0xB3 && cell(5, 2) == 'q', "ESC ( 0 maps line drawing, ESC ( B restores");
  feed("\033]0;window title\007T");
  CHECK(cell(6, 2) == 'T', "OSC payload is skipped up to BEL");
  feed("\033]0;title\033\\U");
  CHECK(cell(7, 2) == 'U', "OSC payload is skipped up to ESC backslash");
  feed("\033\rV");
  CHECK(cell(0, 2) == 'V', "a CR after ESC abandons the sequence and is executed");
  feed("\033[1;1H\033[5;\xC3\xA9");
  CHECK(cell(0, 0) == 0xC3 && cell(1, 0) == 0xA9, "a byte >= 0x80 inside a CSI drops the sequence and prints");
  feed("\033[1;1H\xC3\xA9");
  CHECK(cell(0, 0) == 0xC3 && cell(1, 0) == 0xA9 && gfx_cursx == 2, "UTF-8 pair shows as two CP437 glyphs");
  feed("\033[1;1H\001\002\x7f");
  CHECK(cell(0, 0) == 1 && cell(1, 0) == 2 && cell(2, 0) == 0x7F, "C0 bytes and DEL print CP437 glyphs");
  feed("\033[1;1H\033[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20mZ");
  CHECK(cell(0, 0) == 'Z', "more than 16 parameters are dropped safely");
  feed("\033[99999999999999C");
  CHECK(gfx_cursx == 79, "huge parameter is clamped");
  feed("\033[1;1H\033[?33h\033[=255h\033[0;0r\033[>c\033[?1049h\033[!p\033[ q\033[1;1HY");
  CHECK(cell(0, 0) == 'Y', "SyncTERM/CTerm private sequences are consumed without effect");
  feed("\033[1;1H\033[\x18Z");
  CHECK(cell(0, 0) == 'Z', "CAN aborts a CSI");
  feed("\033[1;1Hab\033[1\bK");
  CHECK(gfx_cursx == 1 && cell(1, 0) == ' ', "BS inside a CSI executes and the sequence continues (ESC[1K)");

  printf("replies\n");
  sent[0] = 0;
  feed("\033[c");
  CHECK(!strcmp(sent, "\033[?1;0c"), "DA replies ESC[?1;0c");
  sent[0] = 0;
  feed("\033[0c");
  CHECK(!strcmp(sent, "\033[?1;0c"), "ESC[0c replies too");
  sent[0] = 0;
  feed("\033[255;255H\033[6n");
  CHECK(!strcmp(sent, "\033[25;80R"), "DSR 6 reports the clamped position");
  sent[0] = 0;
  feed("\033[5n");
  CHECK(!strcmp(sent, "\033[0n"), "DSR 5 reports OK");
  bells = 0;
  feed("\007");
  CHECK(bells == 1, "BEL rings");

  printf("alternate screen, RIS, origin mode\n");
  fresh();
  feed("\033[3;3Habc\033[?1049h");
  CHECK(row_all(2, ' ') && gfx_cursx == 0 && gfx_cursy == 0, "ESC[?1049h clears and homes");
  feed("xyz\033[?1049l");
  CHECK(row_all(0, ' ') && gfx_cursx == 5 && gfx_cursy == 2, "ESC[?1049l clears and restores the cursor");
  feed("\033[5;10r\033[?6h\033[1;1Ha");
  CHECK(cell(0, 4) == 'a', "origin mode: ESC[1;1H is the region top");
  feed("\033[20;1Hb");
  CHECK(cell(0, 9) == 'b', "origin mode: rows clamp to the region bottom");
  feed("\033[1;31m\033[?25l\033c");
  CHECK(row_all(4, ' ') && row_all(9, ' ') && gfx_cursx == 0 && gfx_cursy == 0, "RIS clears the screen and homes");
  feed("\033[20;1Hc");
  CHECK(cell(0, 19) == 'c' && col(0, 19) == COLOR_LTGRAY, "RIS resets region, origin mode and colours");
  feed("\033[1;5r\033[5;1H\033[25;1H\n");
  CHECK(gfx_cursy == 24, "cursor outside region: LF stays on the last row");
  ansi_reset();
  feed("\033[25;1H\n");
  CHECK(gfx_cursy == 24, "ansi_reset restores the full-screen region");

  printf("\n%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
