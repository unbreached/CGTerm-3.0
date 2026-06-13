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
  ANSI_ESC,
  ANSI_CSI
};

static enum ansi_state state;
static int params[8];
static int param_count;
static int current_param;
static int ansi_bold;

/* ANSI base colors (30-37) -> C64 color indices */
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

static int ansi_fg_index = 7;  /* current ANSI fg color index (0-7), default white */
static int saved_x = 0, saved_y = 0;
static int scroll_top = 0, scroll_bottom = 24;  /* scroll region */
static int private_mode = 0;  /* 1 if ESC[? prefix detected */

/* 256-color palette (indices 0-255) mapped to RGB, stored as C64 palette indices
 * for the 16 base colors, or direct RGB for extended colors.
 * For now, 256-color maps to nearest of our 16 VGA colors. */
static unsigned char color256_to_16[256];


/* In ANSI mode with CP437 font (font slot 2), the font grid is
 * ordered by byte value, so no conversion is needed — the byte
 * IS the screencode. This includes box-drawing chars (0xB0-0xDF). */


/* Reset only the escape-sequence parser state (not colors/cursor/font), so a
 * sequence left dangling by one BBS — or a scroll region / private mode it set
 * — cannot carry into the next connection. Called at the start of every
 * connection from net_connect(). */
void ansi_reset(void) {
  state = ANSI_NORMAL;
  param_count = 0;
  current_param = 0;
  private_mode = 0;
  scroll_top = 0;
  scroll_bottom = cfg_rows - 1;
}


void ansi_init(void) {
  state = ANSI_NORMAL;
  param_count = 0;
  current_param = 0;
  ansi_bold = 0;
  ansi_fg_index = 7;
  gfx_fgcolor(COLOR_LTGRAY);
  gfx_set_cell_bg(0);
  gfx_setfont(2);  /* CP437 font for ANSI */
  scroll_top = 0;
  scroll_bottom = cfg_rows - 1;
  private_mode = 0;

  /* Build 256-color to 16-color mapping table */
  {
    /* 0-7: standard colors */
    static const unsigned char base16[] = {
      COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BROWN,
      COLOR_BLUE, COLOR_PURPLE, COLOR_CYAN, COLOR_LTGRAY,
      COLOR_DKGRAY, COLOR_LTRED, COLOR_LTGREEN, COLOR_YELLOW,
      COLOR_LTBLUE, 8/*lt magenta*/, 12/*lt cyan*/, COLOR_WHITE
    };
    int ci;
    for (ci = 0; ci < 16; ci++) color256_to_16[ci] = base16[ci];
    /* 16-231: 6x6x6 color cube — map to nearest base color */
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
}


static void ansi_sgr(void) {
  int i;

  if (param_count == 0) {
    /* ESC[m with no params = reset */
    ansi_bold = 0;
    ansi_fg_index = 7;
    gfx_fgcolor(COLOR_LTGRAY);
    gfx_set_cell_bg(0);
    rvson = 0;
    return;
  }

  for (i = 0; i < param_count; i++) {
    int p = params[i];

    if (p == 0) {
      /* Reset */
      ansi_bold = 0;
      ansi_fg_index = 7;
      gfx_fgcolor(COLOR_LTGRAY);
      gfx_set_cell_bg(0);
      rvson = 0;
    } else if (p == 1) {
      /* Bold */
      ansi_bold = 1;
      gfx_fgcolor(ansi_bold_map[ansi_fg_index]);
    } else if (p == 7) {
      /* Reverse video */
      rvson = 1;
    } else if (p == 22) {
      /* Normal intensity (unbold) */
      ansi_bold = 0;
      gfx_fgcolor(ansi_color_map[ansi_fg_index]);
    } else if (p == 27) {
      /* Reverse off */
      rvson = 0;
    } else if (p >= 30 && p <= 37) {
      /* Foreground color */
      ansi_fg_index = p - 30;
      if (ansi_bold) {
        gfx_fgcolor(ansi_bold_map[ansi_fg_index]);
      } else {
        gfx_fgcolor(ansi_color_map[ansi_fg_index]);
      }
    } else if (p == 39) {
      /* Default foreground */
      ansi_fg_index = 7;
      gfx_fgcolor(ansi_bold ? COLOR_WHITE : COLOR_LTGRAY);
    } else if (p == 38 && i + 2 < param_count && params[i+1] == 5) {
      /* 256-color foreground: ESC[38;5;Nm */
      gfx_fgcolor(color256_to_16[params[i+2] & 0xFF]);
      i += 2;
    } else if (p >= 40 && p <= 47) {
      /* Background color */
      gfx_set_cell_bg(ansi_color_map[p - 40]);
    } else if (p == 48 && i + 2 < param_count && params[i+1] == 5) {
      /* 256-color background: ESC[48;5;Nm */
      gfx_set_cell_bg(color256_to_16[params[i+2] & 0xFF]);
      i += 2;
    } else if (p == 49) {
      /* Default background */
      gfx_set_cell_bg(0);
    } else if (p >= 90 && p <= 97) {
      /* Bright foreground colors */
      static const unsigned char bright_map[8] = {
        COLOR_DKGRAY, COLOR_LTRED, COLOR_LTGREEN, COLOR_YELLOW,
        COLOR_LTBLUE, 8, 12, COLOR_WHITE
      };
      gfx_fgcolor(bright_map[p - 90]);
    } else if (p >= 100 && p <= 107) {
      /* Bright background colors */
      static const unsigned char bright_map[8] = {
        COLOR_DKGRAY, COLOR_LTRED, COLOR_LTGREEN, COLOR_YELLOW,
        COLOR_LTBLUE, 8, 12, COLOR_WHITE
      };
      gfx_set_cell_bg(bright_map[p - 100]);
    }
  }
}


static void ansi_dispatch(unsigned char cmd) {
  int n, row, col, i;

  n = (param_count > 0 && params[0] > 0) ? params[0] : 1;
  /* Clamp cursor-movement repeat counts to the terminal dimensions so a
   * malicious server can't freeze the UI with huge repeat values. */
  {
    int maxn = (cfg_rows > cfg_columns ? cfg_rows : cfg_columns);
    if (n > maxn) n = maxn;
  }

  switch (cmd) {

  case 'A':  /* Cursor up */
    for (i = 0; i < n; i++) gfx_cursup();
    break;

  case 'B':  /* Cursor down */
    for (i = 0; i < n; i++) gfx_cursdown();
    break;

  case 'C':  /* Cursor forward */
    for (i = 0; i < n; i++) gfx_cursright();
    break;

  case 'D':  /* Cursor back */
    for (i = 0; i < n; i++) gfx_cursleft();
    break;

  case 'H':  /* Cursor position */
  case 'f':
    row = (param_count > 0 && params[0] > 0) ? params[0] - 1 : 0;
    col = (param_count > 1 && params[1] > 0) ? params[1] - 1 : 0;
    if (row >= cfg_rows) row = cfg_rows - 1;
    if (col >= cfg_columns) col = cfg_columns - 1;
    gfx_setcursxy(col, row);
    break;

  case 'J':  /* Erase in display */
    n = (param_count > 0) ? params[0] : 0;
    if (n == 0) {
      /* Clear from cursor to end of display */
      int save_x = gfx_cursx, save_y = gfx_cursy, x, y;
      for (x = save_x; x < cfg_columns; x++) {
        gfx_setcursxy(x, save_y);
        gfx_draw_char(32);
      }
      for (y = save_y + 1; y < cfg_rows; y++)
        gfx_clear_line(y, gfx_get_fgcolor());
      gfx_setcursxy(save_x, save_y);
    } else if (n == 1) {
      /* Clear from start to cursor */
      int save_x = gfx_cursx, save_y = gfx_cursy, x, y;
      for (y = 0; y < save_y; y++)
        gfx_clear_line(y, gfx_get_fgcolor());
      for (x = 0; x <= save_x; x++) {
        gfx_setcursxy(x, save_y);
        gfx_draw_char(32);
      }
      gfx_setcursxy(save_x, save_y);
    } else if (n == 2) {
      gfx_cls();
      gfx_setcursxy(0, 0);
    }
    break;

  case 'K':  /* Erase in line */
    n = (param_count > 0) ? params[0] : 0;
    {
      int save_x = gfx_cursx, x;
      if (n == 0) {
        /* Clear from cursor to end of line */
        for (x = save_x; x < cfg_columns; x++) {
          gfx_setcursxy(x, gfx_cursy);
          gfx_draw_char(32);
        }
      } else if (n == 1) {
        /* Clear from start of line to cursor */
        for (x = 0; x <= save_x; x++) {
          gfx_setcursxy(x, gfx_cursy);
          gfx_draw_char(32);
        }
      } else if (n == 2) {
        /* Clear entire line */
        gfx_clear_line(gfx_cursy, gfx_get_fgcolor());
      }
      gfx_setcursxy(save_x, gfx_cursy);
    }
    break;

  case 'L':  /* Insert lines */
    /* Per VT spec IL is a no-op when the cursor is outside the scroll region */
    if (gfx_cursy < scroll_top || gfx_cursy > scroll_bottom) break;
    /* Scroll down from cursor, inserting blank lines */
    for (i = 0; i < n; i++) {
      int y;
      for (y = scroll_bottom; y > gfx_cursy; y--) {
        gfx_scroll_line(y - 1, y);
      }
      gfx_clear_line(gfx_cursy, gfx_get_fgcolor());
    }
    break;

  case 'M':  /* Delete lines */
    if (gfx_cursy < scroll_top || gfx_cursy > scroll_bottom) break;
    for (i = 0; i < n; i++) {
      int y;
      for (y = gfx_cursy; y < scroll_bottom; y++) {
        gfx_scroll_line(y + 1, y);
      }
      gfx_clear_line(scroll_bottom, gfx_get_fgcolor());
    }
    break;

  case 'S':  /* Scroll up */
    for (i = 0; i < n; i++) gfx_scrollup();
    break;

  case 'T':  /* Scroll down */
    /* Scroll down: move lines down, insert blank at top */
    for (i = 0; i < n; i++) {
      int y;
      for (y = scroll_bottom; y > scroll_top; y--) {
        gfx_scroll_line(y - 1, y);
      }
      gfx_clear_line(scroll_top, gfx_get_fgcolor());
    }
    break;

  case 'h':  /* Set mode */
    if (private_mode) {
      if (param_count > 0 && params[0] == 25)
        gfx_setcursxy(gfx_cursx, gfx_cursy);  /* show cursor (already visible) */
      /* ?1000 mouse tracking etc — silently ignore */
    }
    break;

  case 'l':  /* Reset mode */
    if (private_mode) {
      if (param_count > 0 && params[0] == 25)
        gfx_setcursxy(-1, -1);  /* hide cursor */
    }
    break;

  case 'm':  /* SGR — Select Graphic Rendition */
    ansi_sgr();
    break;

  case 'n':  /* Device Status Report */
    if (param_count > 0 && params[0] == 6) {
      char response[32];
      snprintf(response, sizeof(response), "\033[%d;%dR", gfx_cursy + 1, gfx_cursx + 1);
      net_send_string((const unsigned char *)response);
    }
    break;

  case 'r':  /* Set scroll region (DECSTBM) */
    scroll_top = (param_count > 0 && params[0] > 0) ? params[0] - 1 : 0;
    scroll_bottom = (param_count > 1 && params[1] > 0) ? params[1] - 1 : cfg_rows - 1;
    if (scroll_top >= cfg_rows) scroll_top = 0;
    if (scroll_bottom >= cfg_rows) scroll_bottom = cfg_rows - 1;
    if (scroll_top >= scroll_bottom) { scroll_top = 0; scroll_bottom = cfg_rows - 1; }
    gfx_setcursxy(0, 0);
    break;

  case 's':  /* Save cursor position */
    saved_x = gfx_cursx;
    saved_y = gfx_cursy;
    break;

  case 'u':  /* Restore cursor position */
    gfx_setcursxy(saved_x, saved_y);
    break;

  case 'c':  /* Device Attributes */
    if (param_count == 0 || params[0] == 0) {
      /* Report as VT102 */
      net_send_string((const unsigned char *)"\033[?6c");
    }
    break;
  }
}


void ansi_out(unsigned char byte) {

  switch (state) {

  case ANSI_NORMAL:
    if (byte == 27) {
      /* ESC */
      state = ANSI_ESC;
    } else if (byte == 13) {
      /* CR */
      gfx_setcursx(0);
    } else if (byte == 10) {
      /* LF */
      gfx_cursdown();
    } else if (byte == 8) {
      /* BS */
      gfx_cursleft();
    } else if (byte == 7) {
      /* BEL */
      sound_play_sample(sound_bell);
    } else if (byte == 12) {
      /* FF (Form Feed) — clear screen */
      gfx_cls();
      gfx_setcursxy(0, 0);
    } else if (byte == 9) {
      /* TAB — advance to next 8-column boundary */
      int target = (gfx_cursx + 8) & ~7;
      if (target >= cfg_columns) target = cfg_columns - 1;
      gfx_setcursxy(target, gfx_cursy);
    } else if (byte >= 32) {
      /* Printable — CP437 font is byte-ordered, includes box-drawing 128-255.
       * Note: reverse video via bit 7 not available for chars > 127. */
      gfx_draw_char((byte < 128) ? (byte + rvson * 128) : byte);
      gfx_cursadvance();
    }
    break;

  case ANSI_ESC:
    if (byte == '[') {
      state = ANSI_CSI;
      param_count = 0;
      current_param = 0;
      private_mode = 0;
      memset(params, 0, sizeof(params));
    } else {
      /* Unknown ESC sequence, discard */
      state = ANSI_NORMAL;
    }
    break;

  case ANSI_CSI:
    if (byte == '?' && param_count == 0 && current_param == 0) {
      /* Private mode prefix */
      private_mode = 1;
    } else if (byte >= '0' && byte <= '9') {
      /* Accumulate digit, clamped to avoid signed overflow / DoS loops. */
      if (current_param < 65535) {
        current_param = current_param * 10 + (byte - '0');
        if (current_param > 65535) current_param = 65535;
      }
    } else if (byte == ';') {
      /* Parameter separator */
      if (param_count < 8) {
        params[param_count++] = current_param;
      }
      current_param = 0;
    } else if (byte >= 0x40 && byte <= 0x7E) {
      /* Final byte — store last param and dispatch */
      if (param_count < 8) {
        params[param_count++] = current_param;
      }
      ansi_dispatch(byte);
      state = ANSI_NORMAL;
    } else {
      /* Intermediate bytes (0x20-0x2F) or unknown — ignore */
      if (byte < 0x20 || byte > 0x3F) {
        state = ANSI_NORMAL;
      }
    }
    break;
  }
}
