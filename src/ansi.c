/* ANSI/VT terminal emulation for CGTerm.
 *
 * The screen model lives in gfx.c: gfx_0400 holds the glyph (in ANSI mode the
 * CP437 font is byte-ordered, so the byte IS the glyph index), gfx_d800 the
 * foreground colour and gfx_bg the per-cell background colour. The cursor is
 * gfx_cursx/gfx_cursy, the visible size cfg_columns x cfg_rows.
 *
 * The emulation follows what modern BBS software (Mystic, Synchronet, Enigma,
 * WWIV) and their reference terminal (SyncTERM/CTerm) expect: a VT100-style
 * scroll region, DECAWM auto-wrap with the "pending wrap" last-column state,
 * BCE (erase with the current background colour), and the iCE-colour
 * convention where SGR 5 (blink) selects a bright background.
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "gfx.h"
#include "kernal.h"
#include "sound.h"
#include "config.h"
#include "net.h"
#include "ansi.h"


enum ansi_state {
  ANSI_NORMAL = 0,
  ANSI_ESC,        /* got ESC, waiting for the next byte */
  ANSI_ESC_INTER,  /* ESC + intermediate (0x20-0x2F), e.g. ESC ( ; one final byte follows */
  ANSI_CSI,        /* ESC [ parameters ... final byte */
  ANSI_STRING      /* OSC/DCS/APC/PM/SOS: skip until BEL or ESC \ (ST) */
};

#define ANSI_MAX_PARAMS 16
#define ANSI_STRING_LIMIT 4096   /* give up on an unterminated OSC/DCS after this */

static enum ansi_state state;
static int params[ANSI_MAX_PARAMS];   /* 38;2;r;g;b;48;2;r;g;b needs 10 */
static int param_count;
static int current_param;
static int csi_prefix;      /* 0 or the private marker: '?', '=', '>', '<' */
static int csi_inter;       /* intermediate byte inside CSI (e.g. '!' in ESC[!p), 0 if none */
static int esc_inter;       /* intermediate byte after ESC, e.g. '(' for charset selection */
static int string_len;      /* bytes consumed inside ANSI_STRING */

/* ANSI base colors (30-37 / 40-47) -> C64 palette slot indices */
static const unsigned char ansi_color_map[8] = {
  COLOR_BLACK,    /* 30: black */
  COLOR_RED,      /* 31: red */
  COLOR_GREEN,    /* 32: green */
  COLOR_BROWN,    /* 33: yellow (brown when not bold) */
  COLOR_BLUE,     /* 34: blue */
  COLOR_PURPLE,   /* 35: magenta */
  COLOR_CYAN,     /* 36: cyan */
  COLOR_LTGRAY    /* 37: white (light gray) */
};

/* Bright variants: SGR 1 foreground, SGR 5 (iCE) background, 90-97, 100-107 */
static const unsigned char ansi_bold_map[8] = {
  COLOR_DKGRAY,   /* 30+bold: dark gray */
  COLOR_LTRED,    /* 31+bold: light red */
  COLOR_LTGREEN,  /* 32+bold: light green */
  COLOR_YELLOW,   /* 33+bold: yellow */
  COLOR_LTBLUE,   /* 34+bold: light blue */
  8,              /* 35+bold: light magenta (VGA palette slot 8) */
  12,             /* 36+bold: light cyan (VGA palette slot 12) */
  COLOR_WHITE     /* 37+bold: bright white */
};

/* SGR attribute state. fg/bg index is 0-7 for a colour chosen with 30-37 /
 * 40-47 (so bold / blink can re-derive the bright variant later), or -1 when
 * the colour came from 90-97, 100-107, 38;5, 48;5, 38;2 or 48;2 and is final. */
static int ansi_bold;
static int ansi_blink;         /* SGR 5: rendered as bright background (iCE colours) */
static int ansi_rvs;           /* SGR 7 reverse video active */
static int ansi_fg_index = 7;
static int ansi_bg_index = 0;
static unsigned char ansi_cur_fg = COLOR_LTGRAY;  /* C64/VGA palette index */
static unsigned char ansi_cur_bg = 0;

/* Terminal modes */
static int autowrap = 1;       /* DECAWM (ESC[?7h / ESC[?7l) */
static int wrap_pending;       /* VT "last column" state: a glyph was written in
                                * the last column; the wrap happens only when
                                * the next printable character arrives. */
static int origin_mode;        /* DECOM (ESC[?6h): CUP/home are relative to the region */
static int alt_screen;         /* ESC[?1049h / ESC[?47h active */
static int g0_graphics;        /* ESC ( 0 selected the DEC line-drawing set for G0 */

static int saved_x = 0, saved_y = 0;            /* ESC[s / ESC[u (SCOSC/SCORC) */
static int scroll_top = 0, scroll_bottom = 24;  /* DECSTBM scroll region, 0-based inclusive */

/* DECSC / DECRC (ESC 7 / ESC 8): cursor, attributes, charset and wrap state */
static struct {
  int valid;
  int x, y;
  int bold, blink, rvs, fg_index, bg_index;
  unsigned char fg, bg;
  int graphics, wrap, origin;
} dec_saved;

/* 256-colour palette index -> one of our 16 palette slots */
static unsigned char color256_to_16[256];


/* ---- colours ---------------------------------------------------------- */

/* Reverse video is rendered by swapping the colours of the cell, not by
 * selecting glyph+128: the CP437 font is byte-ordered, so 'A'+128 is a
 * box-drawing character, which is what SGR 7 used to show. */
static unsigned char ansi_eff_fg(void) {
  return ansi_rvs ? ansi_cur_bg : ansi_cur_fg;
}

static unsigned char ansi_eff_bg(void) {
  return ansi_rvs ? ansi_cur_fg : ansi_cur_bg;
}

static void ansi_apply_colors(void) {
  gfx_fgcolor(ansi_eff_fg());
  gfx_set_cell_bg(ansi_eff_bg());
}

/* Re-derive the palette colour of an indexed (30-37) foreground from bold */
static void ansi_update_fg(void) {
  if (ansi_fg_index >= 0) {
    ansi_cur_fg = ansi_bold ? ansi_bold_map[ansi_fg_index] : ansi_color_map[ansi_fg_index];
  }
  ansi_apply_colors();
}

/* Re-derive the palette colour of an indexed (40-47) background from blink:
 * BBS art uses SGR 5 as "bright background" (iCE colours) rather than blinking. */
static void ansi_update_bg(void) {
  if (ansi_bg_index >= 0) {
    ansi_cur_bg = ansi_blink ? ansi_bold_map[ansi_bg_index] : ansi_color_map[ansi_bg_index];
  }
  ansi_apply_colors();
}

static void ansi_set_fg_direct(unsigned char c) {
  ansi_fg_index = -1;
  ansi_cur_fg = c;
  ansi_apply_colors();
}

static void ansi_set_bg_direct(unsigned char c) {
  ansi_bg_index = -1;
  ansi_cur_bg = c;
  ansi_apply_colors();
}

static void ansi_sgr_reset(void) {
  ansi_bold = 0;
  ansi_blink = 0;
  ansi_rvs = 0;
  ansi_fg_index = 7;
  ansi_bg_index = 0;
  ansi_cur_fg = COLOR_LTGRAY;
  ansi_cur_bg = 0;
  ansi_apply_colors();
}

/* Nearest of the 16 ANSI colours for a truecolour (SGR 38;2 / 48;2) triple,
 * returned as a palette index via the 256-colour table (entries 0-15). */
static unsigned char ansi_rgb_to_16(int r, int g, int b) {
  static const int ansi_rgb[16][3] = {
    {0,0,0},{170,0,0},{0,170,0},{170,85,0},{0,0,170},{170,0,170},{0,170,170},{170,170,170},
    {85,85,85},{255,85,85},{85,255,85},{255,255,85},{85,85,255},{255,85,255},{85,255,255},{255,255,255}
  };
  int i, best = 7;
  long bestd = 1L << 30;
  for (i = 0; i < 16; i++) {
    long dr = r - ansi_rgb[i][0], dg = g - ansi_rgb[i][1], db = b - ansi_rgb[i][2];
    long d = dr * dr + dg * dg + db * db;
    if (d < bestd) { bestd = d; best = i; }
  }
  return color256_to_16[best];
}

static void ansi_build_color_table(void) {
  /* 0-7: standard colors, 8-15: bright */
  static const unsigned char base16[] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BROWN,
    COLOR_BLUE, COLOR_PURPLE, COLOR_CYAN, COLOR_LTGRAY,
    COLOR_DKGRAY, COLOR_LTRED, COLOR_LTGREEN, COLOR_YELLOW,
    COLOR_LTBLUE, 8/*lt magenta*/, 12/*lt cyan*/, COLOR_WHITE
  };
  int ci;
  for (ci = 0; ci < 16; ci++) color256_to_16[ci] = base16[ci];
  /* 16-231: 6x6x6 color cube: map to nearest base color */
  for (ci = 16; ci < 232; ci++) {
    int idx = ci - 16;
    int r = (idx / 36) * 51, g = ((idx / 6) % 6) * 51, b = (idx % 6) * 51;
    /* Simple nearest: pick based on dominant channel */
    if (r > 170 && g < 85 && b < 85) color256_to_16[ci] = COLOR_LTRED;
    else if (g > 170 && r < 85 && b < 85) color256_to_16[ci] = COLOR_LTGREEN;
    else if (b > 170 && r < 85 && g < 85) color256_to_16[ci] = COLOR_LTBLUE;
    else if (r > 170 && g > 170 && b < 85) color256_to_16[ci] = COLOR_YELLOW;
    else if (r > 170 && b > 170 && g < 85) color256_to_16[ci] = 8; /* lt magenta */
    else if (g > 170 && b > 170 && r < 85) color256_to_16[ci] = 12; /* lt cyan */
    else if (r > 170 && g > 170 && b > 170) color256_to_16[ci] = COLOR_WHITE;
    else if (r > 85 || g > 85 || b > 85) color256_to_16[ci] = COLOR_LTGRAY;
    else if (r > 40 || g > 40 || b > 40) color256_to_16[ci] = COLOR_DKGRAY;
    else color256_to_16[ci] = COLOR_BLACK;
  }
  /* 232-255: grayscale ramp */
  for (ci = 232; ci < 256; ci++) {
    int gray = (ci - 232) * 10 + 8;
    if (gray > 192) color256_to_16[ci] = COLOR_WHITE;
    else if (gray > 128) color256_to_16[ci] = COLOR_LTGRAY;
    else if (gray > 64) color256_to_16[ci] = COLOR_DKGRAY;
    else color256_to_16[ci] = COLOR_BLACK;
  }
}


/* ---- cursor / scrolling primitives ------------------------------------ */

/* Every explicit cursor placement clears the pending-wrap state (VT100:
 * "the wrap flag is reset by any cursor movement"). */
static void ansi_move(int x, int y) {
  wrap_pending = 0;
  if (x < 0) x = 0; else if (x >= cfg_columns) x = cfg_columns - 1;
  if (y < 0) y = 0; else if (y >= cfg_rows) y = cfg_rows - 1;
  gfx_setcursxy(x, y);
}

/* Home position: row 0, or the top of the region in origin mode */
static void ansi_home(void) {
  ansi_move(0, origin_mode ? scroll_top : 0);
}

static int ansi_region_is_full_screen(void) {
  return scroll_top == 0 && scroll_bottom == cfg_rows - 1;
}

/* Scroll the region up by n. When the region is the whole screen the line
 * that leaves at the top goes into the scrollback buffer (gfx_scrollup);
 * a partial region never touches scrollback. Vacated rows are blank in the
 * current colours (BCE). */
static void ansi_scroll_up(int n) {
  int i;
  if (n <= 0) return;
  if (ansi_region_is_full_screen()) {
    if (n > cfg_rows) n = cfg_rows;
    for (i = 0; i < n; i++) {
      gfx_scrollup();
      gfx_fill_cells(0, cfg_rows - 1, cfg_columns, ansi_eff_fg(), ansi_eff_bg());
    }
  } else {
    gfx_scroll_region(scroll_top, scroll_bottom, n, ansi_eff_fg(), ansi_eff_bg());
  }
}

/* Scroll the region down by n (SD, RI at the top margin, IL) */
static void ansi_scroll_down(int n) {
  if (n <= 0) return;
  gfx_scroll_region(scroll_top, scroll_bottom, -n, ansi_eff_fg(), ansi_eff_bg());
}

/* IND / LF: move down one row. At the bottom margin the region scrolls;
 * below the region (cursor outside it) the cursor stops at the last screen
 * row without scrolling, as on a VT100. */
static void ansi_index(void) {
  wrap_pending = 0;
  if (gfx_cursy == scroll_bottom) {
    ansi_scroll_up(1);
  } else if (gfx_cursy < cfg_rows - 1) {
    gfx_setcursxy(gfx_cursx, gfx_cursy + 1);
  }
}

/* RI (ESC M): move up one row; at the top margin the region scrolls down */
static void ansi_reverse_index(void) {
  wrap_pending = 0;
  if (gfx_cursy == scroll_top) {
    ansi_scroll_down(1);
  } else if (gfx_cursy > 0) {
    gfx_setcursxy(gfx_cursx, gfx_cursy - 1);
  }
}

/* NEL (ESC E): CR + IND */
static void ansi_next_line(void) {
  gfx_setcursxy(0, gfx_cursy);
  ansi_index();
}

/* Blank an area with BCE: the current foreground and background colours */
static void ansi_erase_cells(int x, int y, int count) {
  gfx_fill_cells(x, y, count, ansi_eff_fg(), ansi_eff_bg());
}

static void ansi_erase_rows(int from, int to) {
  int y;
  for (y = from; y <= to; y++) {
    ansi_erase_cells(0, y, cfg_columns);
  }
}

static void ansi_erase_screen(void) {
  ansi_erase_rows(0, cfg_rows - 1);
}

/* DEC special graphics (ESC ( 0): map the VT100 line-drawing set 0x60-0x7E
 * to the CP437 glyphs the ANSI font already has. */
static unsigned char ansi_dec_graphics(unsigned char c) {
  static const unsigned char map[31] = {
    0x04, /* ` diamond */
    0xB1, /* a checkerboard */
    'b', 'c', 'd', 'e',   /* HT FF CR LF symbols: no CP437 equivalent */
    0xF8, /* f degree */
    0xF1, /* g plus/minus */
    0xB0, /* h board of squares */
    'i',  /* VT symbol */
    0xD9, /* j lower right corner */
    0xBF, /* k upper right corner */
    0xDA, /* l upper left corner */
    0xC0, /* m lower left corner */
    0xC5, /* n crossing lines */
    0xC4, /* o scan line 1 */
    0xC4, /* p scan line 3 */
    0xC4, /* q horizontal line */
    0xC4, /* r scan line 7 */
    0xC4, /* s scan line 9 */
    0xC3, /* t left tee */
    0xB4, /* u right tee */
    0xC1, /* v bottom tee */
    0xC2, /* w top tee */
    0xB3, /* x vertical line */
    0xF3, /* y less or equal */
    0xF2, /* z greater or equal */
    0xE3, /* { pi */
    0xF0, /* | not equal (approximated) */
    0x9C, /* } pound sterling */
    0xFA  /* ~ centered dot */
  };
  if (c >= 0x60 && c <= 0x7E) {
    return map[c - 0x60];
  }
  return c;
}

/* Print one glyph with DECAWM semantics. Writing in the last column does not
 * move the cursor; it sets wrap_pending instead, so an 80-column art line
 * that ends exactly in column 80 followed by CR LF does not produce a blank
 * row. The deferred wrap (CR + IND) happens when the next glyph arrives. With
 * auto-wrap off the cursor sticks in the last column and overwrites. */
static void ansi_print(unsigned char glyph) {
  if (wrap_pending) {
    wrap_pending = 0;
    gfx_setcursxy(0, gfx_cursy);
    ansi_index();
  }
  gfx_draw_char(glyph);
  if (gfx_cursx < cfg_columns - 1) {
    gfx_setcursxy(gfx_cursx + 1, gfx_cursy);
  } else if (autowrap) {
    wrap_pending = 1;
  }
}

/* DECSC: save cursor position, attributes, charset and the wrap/origin flags */
static void ansi_save_cursor(void) {
  dec_saved.valid = 1;
  dec_saved.x = gfx_cursx;
  dec_saved.y = gfx_cursy;
  dec_saved.bold = ansi_bold;
  dec_saved.blink = ansi_blink;
  dec_saved.rvs = ansi_rvs;
  dec_saved.fg_index = ansi_fg_index;
  dec_saved.bg_index = ansi_bg_index;
  dec_saved.fg = ansi_cur_fg;
  dec_saved.bg = ansi_cur_bg;
  dec_saved.graphics = g0_graphics;
  dec_saved.wrap = wrap_pending;
  dec_saved.origin = origin_mode;
}

/* DECRC: restore what DECSC saved; with nothing saved it homes the cursor
 * and resets attributes, as the VT spec requires. */
static void ansi_restore_cursor(void) {
  if (!dec_saved.valid) {
    ansi_sgr_reset();
    ansi_home();
    return;
  }
  ansi_bold = dec_saved.bold;
  ansi_blink = dec_saved.blink;
  ansi_rvs = dec_saved.rvs;
  ansi_fg_index = dec_saved.fg_index;
  ansi_bg_index = dec_saved.bg_index;
  ansi_cur_fg = dec_saved.fg;
  ansi_cur_bg = dec_saved.bg;
  g0_graphics = dec_saved.graphics;
  origin_mode = dec_saved.origin;
  ansi_apply_colors();
  ansi_move(dec_saved.x, dec_saved.y);
  wrap_pending = dec_saved.wrap;
}

/* DECSTR (ESC[!p) and the mode part of RIS: modes, region, attributes */
static void ansi_soft_reset(void) {
  autowrap = 1;
  wrap_pending = 0;
  origin_mode = 0;
  g0_graphics = 0;
  scroll_top = 0;
  scroll_bottom = cfg_rows - 1;
  dec_saved.valid = 0;
  saved_x = saved_y = 0;
  ansi_sgr_reset();
  gfx_cursor_show(1);
}


/* ---- init / reset ----------------------------------------------------- */

static void ansi_parser_reset(void) {
  state = ANSI_NORMAL;
  param_count = 0;
  current_param = 0;
  csi_prefix = 0;
  csi_inter = 0;
  esc_inter = 0;
  string_len = 0;
}

/* Reset the escape-sequence parser and the per-session modes (not colours,
 * cursor or font), so a sequence left dangling by one BBS, or a scroll
 * region / wrap mode it set, cannot carry into the next connection. Called
 * at the start of every connection from net_connect(). */
void ansi_reset(void) {
  ansi_parser_reset();
  scroll_top = 0;
  scroll_bottom = cfg_rows - 1;
  autowrap = 1;
  wrap_pending = 0;
  origin_mode = 0;
  alt_screen = 0;
  g0_graphics = 0;
  ansi_rvs = 0;
  ansi_apply_colors();
  gfx_cursor_show(1);
}


void ansi_init(void) {
  ansi_build_color_table();
  ansi_parser_reset();
  alt_screen = 0;
  ansi_soft_reset();
  gfx_setfont(2);  /* CP437 font for ANSI */
}

/* RIS (ESC c): full reset: parser, modes, attributes, screen, cursor */
static void ansi_hard_reset(void) {
  ansi_parser_reset();
  alt_screen = 0;
  ansi_soft_reset();
  ansi_erase_screen();
  ansi_move(0, 0);
}


/* ---- SGR -------------------------------------------------------------- */

static void ansi_sgr(void) {
  int i;

  if (param_count == 0) {
    ansi_sgr_reset();   /* ESC[m with no params = reset */
    return;
  }

  for (i = 0; i < param_count; i++) {
    int p = params[i];

    if (p == 0) {
      ansi_sgr_reset();
    } else if (p == 1) {
      /* Bold: bright foreground */
      ansi_bold = 1;
      ansi_update_fg();
    } else if (p == 5 || p == 6) {
      /* Blink: BBS art convention (iCE colours) is a bright background */
      ansi_blink = 1;
      ansi_update_bg();
    } else if (p == 7) {
      /* Reverse video */
      ansi_rvs = 1;
      ansi_apply_colors();
    } else if (p == 2 || p == 3 || p == 4 || p == 8 || p == 9 ||
               p == 21 || p == 23 || p == 24 || p == 28 || p == 29) {
      /* faint / italic / underline / conceal / strike and their resets:
       * not representable in a 16-colour cell, ignored */
    } else if (p == 22) {
      /* Normal intensity (unbold) */
      ansi_bold = 0;
      ansi_update_fg();
    } else if (p == 25) {
      /* Blink off: back to the normal background */
      ansi_blink = 0;
      ansi_update_bg();
    } else if (p == 27) {
      /* Reverse off */
      ansi_rvs = 0;
      ansi_apply_colors();
    } else if (p >= 30 && p <= 37) {
      /* Foreground color */
      ansi_fg_index = p - 30;
      ansi_update_fg();
    } else if (p == 38 && i + 2 < param_count && params[i+1] == 5) {
      /* 256-color foreground: ESC[38;5;Nm */
      ansi_set_fg_direct(color256_to_16[params[i+2] & 0xFF]);
      i += 2;
    } else if (p == 38 && i + 4 < param_count && params[i+1] == 2) {
      /* truecolor foreground: ESC[38;2;r;g;bm (Mystic/Synchronet themes) */
      ansi_set_fg_direct(ansi_rgb_to_16(params[i+2] & 0xFF, params[i+3] & 0xFF, params[i+4] & 0xFF));
      i += 4;
    } else if (p == 38) {
      /* malformed 38: skip the rest, it is not safe to interpret */
      break;
    } else if (p == 39) {
      /* Default foreground */
      ansi_fg_index = 7;
      ansi_update_fg();
    } else if (p >= 40 && p <= 47) {
      /* Background color */
      ansi_bg_index = p - 40;
      ansi_update_bg();
    } else if (p == 48 && i + 2 < param_count && params[i+1] == 5) {
      /* 256-color background: ESC[48;5;Nm */
      ansi_set_bg_direct(color256_to_16[params[i+2] & 0xFF]);
      i += 2;
    } else if (p == 48 && i + 4 < param_count && params[i+1] == 2) {
      ansi_set_bg_direct(ansi_rgb_to_16(params[i+2] & 0xFF, params[i+3] & 0xFF, params[i+4] & 0xFF));
      i += 4;
    } else if (p == 48) {
      break;
    } else if (p == 49) {
      /* Default background */
      ansi_bg_index = 0;
      ansi_update_bg();
    } else if (p >= 90 && p <= 97) {
      /* Bright foreground colors (aixterm) */
      ansi_set_fg_direct(ansi_bold_map[p - 90]);
    } else if (p >= 100 && p <= 107) {
      /* Bright background colors (aixterm) */
      ansi_set_bg_direct(ansi_bold_map[p - 100]);
    }
    /* anything else: unknown SGR, ignored */
  }
}


/* ---- modes (ESC[?...h / ESC[?...l) ------------------------------------ */

static void ansi_private_mode(int set) {
  int i;

  for (i = 0; i < param_count; i++) {
    switch (params[i]) {

    case 6:   /* DECOM origin mode: cursor addressing relative to the region */
      origin_mode = set;
      ansi_home();
      break;

    case 7:   /* DECAWM auto-wrap */
      autowrap = set;
      if (!set) wrap_pending = 0;
      break;

    case 25:  /* DECTCEM cursor visibility */
      gfx_cursor_show(set);
      break;

    case 47:    /* alternate screen (xterm) */
    case 1047:
    case 1049:
      /* No second buffer: entering clears the screen (saving the cursor for
       * 1049), leaving clears it again and restores the cursor. */
      if (set && !alt_screen) {
        alt_screen = 1;
        if (params[i] == 1049) ansi_save_cursor();
        ansi_erase_screen();
        ansi_move(0, 0);
      } else if (!set && alt_screen) {
        alt_screen = 0;
        ansi_erase_screen();
        if (params[i] == 1049) ansi_restore_cursor();
        else ansi_move(0, 0);
      }
      break;

    default:
      /* ?1 cursor keys, ?12 blink, ?33 SyncTERM bright-background (we always
       * render blink as bright), ?1000-?1006 mouse, ?2004 bracketed paste,
       * ?9 / ?3 etc.: ignored */
      break;
    }
  }
}


/* ---- CSI dispatch ----------------------------------------------------- */

/* Parameter i, or def when missing or zero (ECMA-48 default handling) */
static int ansi_param(int i, int def) {
  return (i < param_count && params[i] > 0) ? params[i] : def;
}

/* Parameter i as given (0 when missing), for ED/EL/DSR selectors */
static int ansi_param_raw(int i) {
  return (i < param_count) ? params[i] : 0;
}

/* Clamp a repeat count to the terminal size so a malicious server cannot
 * freeze the UI with ESC[65535@ style counts. */
static int ansi_count(int i) {
  int n = ansi_param(i, 1);
  int maxn = (cfg_rows > cfg_columns ? cfg_rows : cfg_columns);
  if (n > maxn) n = maxn;
  return n;
}

static void ansi_dispatch(unsigned char cmd) {
  int n, row, col;

  /* ESC[!p (DECSTR soft reset) is the only intermediate form we honour;
   * ESC[?$p, ESC[ q (DECSCUSR) and friends are consumed silently. */
  if (csi_inter) {
    if (csi_inter == '!' && cmd == 'p' && !csi_prefix) {
      ansi_soft_reset();
    }
    return;
  }

  /* Private-marker sequences: only ESC[?...h/l mean something here.
   * ESC[=255h (SyncTERM), ESC[>c, ESC[<...: ignored safely. */
  if (csi_prefix) {
    if (csi_prefix == '?' && (cmd == 'h' || cmd == 'l')) {
      ansi_private_mode(cmd == 'h');
    }
    return;
  }

  switch (cmd) {

  /* CUU/CUD/CUF/CUB stop at the margins (VT100/ANSI.SYS); the PETSCII
   * cursor primitives wrap lines and scroll, which mangled ANSI art that
   * uses ESC[40C spacing or ESC[25B. CUU/CUD also stop at the scroll
   * region margins when the cursor starts inside the region. */
  case 'A':  /* CUU cursor up */
    n = ansi_count(0);
    row = gfx_cursy - n;
    if (row < scroll_top && gfx_cursy >= scroll_top) row = scroll_top;
    ansi_move(gfx_cursx, row);
    break;

  case 'B':  /* CUD cursor down */
    n = ansi_count(0);
    row = gfx_cursy + n;
    if (row > scroll_bottom && gfx_cursy <= scroll_bottom) row = scroll_bottom;
    ansi_move(gfx_cursx, row);
    break;

  case 'C':  /* CUF cursor forward */
    ansi_move(gfx_cursx + ansi_count(0), gfx_cursy);
    break;

  case 'D':  /* CUB cursor back */
    ansi_move(gfx_cursx - ansi_count(0), gfx_cursy);
    break;

  case 'E':  /* CNL: down n rows, column 0 */
    n = ansi_count(0);
    row = gfx_cursy + n;
    if (row > scroll_bottom && gfx_cursy <= scroll_bottom) row = scroll_bottom;
    ansi_move(0, row);
    break;

  case 'F':  /* CPL: up n rows, column 0 */
    n = ansi_count(0);
    row = gfx_cursy - n;
    if (row < scroll_top && gfx_cursy >= scroll_top) row = scroll_top;
    ansi_move(0, row);
    break;

  case 'G':  /* CHA cursor horizontal absolute */
  case '`':  /* HPA */
    ansi_move(ansi_param(0, 1) - 1, gfx_cursy);
    break;

  case 'd':  /* VPA vertical position absolute */
    row = ansi_param(0, 1) - 1;
    if (origin_mode) {
      row += scroll_top;
      if (row > scroll_bottom) row = scroll_bottom;
    }
    ansi_move(gfx_cursx, row);
    break;

  case 'H':  /* CUP cursor position */
  case 'f':  /* HVP */
    row = ansi_param(0, 1) - 1;
    col = ansi_param(1, 1) - 1;
    if (origin_mode) {
      /* DECOM: row 1 is the top of the region and the cursor cannot leave it */
      row += scroll_top;
      if (row > scroll_bottom) row = scroll_bottom;
    }
    ansi_move(col, row);
    break;

  case 'J':  /* ED erase in display (BCE: blanks take the current background) */
    n = ansi_param_raw(0);
    wrap_pending = 0;
    if (n == 0) {
      /* cursor to end of display */
      ansi_erase_cells(gfx_cursx, gfx_cursy, cfg_columns - gfx_cursx);
      ansi_erase_rows(gfx_cursy + 1, cfg_rows - 1);
    } else if (n == 1) {
      /* start of display to cursor, inclusive */
      ansi_erase_rows(0, gfx_cursy - 1);
      ansi_erase_cells(0, gfx_cursy, gfx_cursx + 1);
    } else if (n == 2 || n == 3) {
      /* whole screen; the cursor goes home as in ANSI.SYS / CTerm, which is
       * what BBS software written against them expects */
      ansi_erase_screen();
      ansi_move(0, 0);
    }
    break;

  case 'K':  /* EL erase in line (BCE) */
    n = ansi_param_raw(0);
    wrap_pending = 0;
    if (n == 0) {
      ansi_erase_cells(gfx_cursx, gfx_cursy, cfg_columns - gfx_cursx);
    } else if (n == 1) {
      ansi_erase_cells(0, gfx_cursy, gfx_cursx + 1);
    } else if (n == 2) {
      ansi_erase_cells(0, gfx_cursy, cfg_columns);
    }
    break;

  case '@':  /* ICH insert blank characters at the cursor */
    wrap_pending = 0;
    gfx_insert_cells(gfx_cursx, gfx_cursy, ansi_count(0), ansi_eff_fg(), ansi_eff_bg());
    break;

  case 'P':  /* DCH delete characters at the cursor */
    wrap_pending = 0;
    gfx_delete_cells(gfx_cursx, gfx_cursy, ansi_count(0), ansi_eff_fg(), ansi_eff_bg());
    break;

  case 'X':  /* ECH erase characters at the cursor (no shifting) */
    wrap_pending = 0;
    ansi_erase_cells(gfx_cursx, gfx_cursy, ansi_count(0));
    break;

  case 'L':  /* IL insert lines: rows from the cursor to the region bottom move down */
    /* Per VT spec IL/DL are no-ops when the cursor is outside the scroll region */
    wrap_pending = 0;
    if (gfx_cursy < scroll_top || gfx_cursy > scroll_bottom) break;
    gfx_scroll_region(gfx_cursy, scroll_bottom, -ansi_count(0), ansi_eff_fg(), ansi_eff_bg());
    break;

  case 'M':  /* DL delete lines: rows below the cursor move up, blanks enter at the region bottom */
    wrap_pending = 0;
    if (gfx_cursy < scroll_top || gfx_cursy > scroll_bottom) break;
    gfx_scroll_region(gfx_cursy, scroll_bottom, ansi_count(0), ansi_eff_fg(), ansi_eff_bg());
    break;

  case 'S':  /* SU scroll up: content moves up inside the region */
    wrap_pending = 0;
    ansi_scroll_up(ansi_count(0));
    break;

  case 'T':  /* SD scroll down: content moves down inside the region */
    wrap_pending = 0;
    ansi_scroll_down(ansi_count(0));
    break;

  case 'h':  /* SM: IRM / LNM etc, not implemented */
  case 'l':  /* RM */
    break;

  case 'm':  /* SGR select graphic rendition */
    ansi_sgr();
    break;

  case 'n':  /* DSR device status report */
    n = ansi_param_raw(0);
    if (n == 6) {
      /* CPR: cursor position, relative to the region in origin mode.
       * Synchronet sends ESC[255;255H ESC[6n to measure the screen. */
      char response[32];
      row = gfx_cursy + 1;
      if (origin_mode) row -= scroll_top;
      snprintf(response, sizeof(response), "\033[%d;%dR", row, gfx_cursx + 1);
      net_send_string((const unsigned char *)response);
    } else if (n == 5) {
      /* terminal status: OK */
      net_send_string((const unsigned char *)"\033[0n");
    }
    break;

  case 'r':  /* DECSTBM set top and bottom margins */
    /* ESC[r, ESC[0;0r and any invalid pair (top >= bottom) select the full
     * screen. A one-row region is not allowed by the VT spec. The cursor
     * moves home afterwards, as on a real VT100. */
    scroll_top = ansi_param(0, 1) - 1;
    scroll_bottom = ansi_param(1, cfg_rows) - 1;
    if (scroll_bottom > cfg_rows - 1) scroll_bottom = cfg_rows - 1;
    if (scroll_top < 0 || scroll_top >= scroll_bottom) {
      scroll_top = 0;
      scroll_bottom = cfg_rows - 1;
    }
    ansi_home();
    break;

  case 's':  /* SCOSC save cursor position (ANSI.SYS) */
    saved_x = gfx_cursx;
    saved_y = gfx_cursy;
    break;

  case 'u':  /* SCORC restore cursor position */
    ansi_move(saved_x, saved_y);
    break;

  case 'c':  /* DA device attributes */
    if (ansi_param_raw(0) == 0) {
      /* "VT100 with no options": what SyncTERM-aware boards expect */
      net_send_string((const unsigned char *)"\033[?1;0c");
    }
    break;

  default:
    /* Unknown final byte: the sequence has been consumed, nothing happens */
    break;
  }
}


/* ---- control characters ----------------------------------------------- */

/* C0 controls that act in any parser state (VT100 executes them even in the
 * middle of a CSI). Returns 1 if the byte was a control we handle. */
static int ansi_control(unsigned char byte) {
  switch (byte) {
  case 0:    /* NUL: padding, ignored */
    return 1;
  case 7:    /* BEL */
    sound_play_sample(sound_bell);
    return 1;
  case 8:    /* BS: never wraps to the previous line in ANSI; clears pending wrap */
    wrap_pending = 0;
    if (gfx_cursx > 0) gfx_setcursxy(gfx_cursx - 1, gfx_cursy);
    return 1;
  case 9: {  /* HT: next 8-column tab stop, stopping at the right margin */
    int target = (gfx_cursx + 8) & ~7;
    if (target >= cfg_columns) target = cfg_columns - 1;
    ansi_move(target, gfx_cursy);
    return 1;
  }
  case 10:   /* LF */
  case 11:   /* VT */
    ansi_index();
    return 1;
  case 12:   /* FF: ANSI.SYS/CTerm clear screen and home */
    ansi_erase_screen();
    ansi_move(0, 0);
    return 1;
  case 13:   /* CR */
    wrap_pending = 0;
    gfx_setcursxy(0, gfx_cursy);
    return 1;
  case 27:   /* ESC: start (or restart) a sequence */
    state = ANSI_ESC;
    esc_inter = 0;
    return 1;
  default:
    return 0;
  }
}


/* ---- byte input ------------------------------------------------------- */

void ansi_out(unsigned char byte) {

  switch (state) {

  case ANSI_NORMAL:
    if (ansi_control(byte)) {
      break;
    }
    if (byte < 32 || byte == 127) {
      /* Other C0 bytes and DEL are glyphs in CP437 (smileys, arrows, the
       * house at 0x7F) and BBS art uses them as such; SyncTERM prints them. */
      ansi_print(byte);
    } else if (byte >= 0x80) {
      /* 0x80-0xFF: CP437 box drawing, shaded blocks and accented letters. No
       * UTF-8 decoding and no C1 controls: a UTF-8 pair such as C3 A9 shows
       * as two CP437 glyphs instead of being misparsed (0x9B would be CSI). */
      ansi_print(byte);
    } else {
      ansi_print(g0_graphics ? ansi_dec_graphics(byte) : byte);
    }
    break;

  case ANSI_ESC:
    if (byte == '[') {
      state = ANSI_CSI;
      param_count = 0;
      current_param = 0;
      csi_prefix = 0;
      csi_inter = 0;
      memset(params, 0, sizeof(params));
    } else if (byte >= 0x20 && byte <= 0x2F) {
      /* ESC ( / ESC ) / ESC # ...: exactly one final byte follows */
      esc_inter = byte;
      state = ANSI_ESC_INTER;
    } else if (byte == ']' || byte == 'P' || byte == '^' || byte == '_' || byte == 'X') {
      /* OSC / DCS / PM / APC / SOS: a string terminated by BEL or ESC \ */
      state = ANSI_STRING;
      string_len = 0;
    } else if (byte >= 0x30 && byte <= 0x7E) {
      state = ANSI_NORMAL;
      switch (byte) {
      case '7': ansi_save_cursor(); break;             /* DECSC */
      case '8': ansi_restore_cursor(); break;          /* DECRC */
      case 'D': ansi_index(); break;                   /* IND */
      case 'E': ansi_next_line(); break;               /* NEL */
      case 'M': ansi_reverse_index(); break;           /* RI */
      case 'c': ansi_hard_reset(); break;              /* RIS */
      case 'Z':                                        /* DECID: same reply as DA */
        net_send_string((const unsigned char *)"\033[?1;0c");
        break;
      default:
        /* ESC = / ESC > keypad modes, ESC H tab set, and anything else:
         * a two-byte sequence, consumed without eating further data */
        break;
      }
    } else {
      /* A control byte (CR, LF, ...) or a byte >= 0x80 cannot be part of an
       * escape sequence: abandon the ESC and process the byte normally so
       * nothing after a truncated sequence is lost. */
      state = ANSI_NORMAL;
      ansi_out(byte);
    }
    break;

  case ANSI_ESC_INTER:
    if (byte >= 0x20 && byte <= 0x2F) {
      /* further intermediates: keep the first one, it decides the meaning */
    } else if (byte >= 0x30 && byte <= 0x7E) {
      state = ANSI_NORMAL;
      if (esc_inter == '(') {
        /* SCS G0: '0' selects DEC line drawing, 'B' (US ASCII) or anything
         * else returns to the plain CP437 glyph set */
        g0_graphics = (byte == '0');
      }
      /* ESC ) x (G1), ESC # 8 (DECALN) and others: accepted and ignored */
    } else {
      state = ANSI_NORMAL;
      ansi_out(byte);
    }
    break;

  case ANSI_CSI:
    if (byte >= '0' && byte <= '9') {
      /* Accumulate digit, clamped to avoid signed overflow / DoS loops. */
      if (current_param < 65535) {
        current_param = current_param * 10 + (byte - '0');
        if (current_param > 65535) current_param = 65535;
      }
    } else if (byte == ';' || byte == ':') {
      /* Parameter separator (':' is the ITU sub-parameter form some boards
       * use for 38:2:r:g:b; treating it like ';' keeps the colours right). */
      if (param_count < ANSI_MAX_PARAMS) {
        params[param_count++] = current_param;
      }
      current_param = 0;
    } else if (byte >= 0x3C && byte <= 0x3F) {
      /* Private marker '<', '=', '>', '?': only valid before any parameter;
       * elsewhere it makes the sequence invalid and it is dropped. */
      if (param_count == 0 && current_param == 0 && !csi_prefix) {
        csi_prefix = byte;
      } else {
        state = ANSI_NORMAL;
      }
    } else if (byte >= 0x20 && byte <= 0x2F) {
      /* Intermediate byte (space, '!', '$', '"' ...) before the final */
      csi_inter = byte;
    } else if (byte >= 0x40 && byte <= 0x7E) {
      /* Final byte: store the last parameter and dispatch */
      if (param_count < ANSI_MAX_PARAMS) {
        params[param_count++] = current_param;
      }
      state = ANSI_NORMAL;
      ansi_dispatch(byte);
    } else if (byte == 0x18 || byte == 0x1A) {
      /* CAN / SUB abort the sequence */
      state = ANSI_NORMAL;
    } else if (byte < 0x20) {
      /* C0 controls execute in the middle of a sequence (ESC restarts it);
       * other C0 bytes are ignored and the sequence continues */
      if (byte == 27) {
        state = ANSI_ESC;
        esc_inter = 0;
      } else {
        enum ansi_state keep = state;
        if (ansi_control(byte)) {
          state = keep;
        }
      }
    } else {
      /* 0x7F or a byte >= 0x80: not valid in a sequence; drop the sequence
       * and show the byte so art data is not swallowed */
      state = ANSI_NORMAL;
      ansi_out(byte);
    }
    break;

  case ANSI_STRING:
    /* OSC (window title etc.), DCS, APC, PM, SOS: discard the payload up to
     * BEL or ST (ESC \). A string that never terminates is cut off after
     * ANSI_STRING_LIMIT bytes so a broken board cannot blank the session. */
    if (byte == 7) {
      state = ANSI_NORMAL;
    } else if (byte == 27) {
      state = ANSI_ESC;   /* ESC \ (ST) ends it; ESC anything else starts afresh */
      esc_inter = 0;
    } else if (++string_len >= ANSI_STRING_LIMIT) {
      state = ANSI_NORMAL;
    }
    break;
  }
}
