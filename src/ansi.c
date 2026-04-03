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


/* In ANSI mode with CP437 font (font slot 2), the font grid is
 * ordered by byte value, so no conversion is needed — the byte
 * IS the screencode. This includes box-drawing chars (0xB0-0xDF). */


void ansi_init(void) {
  state = ANSI_NORMAL;
  param_count = 0;
  current_param = 0;
  ansi_bold = 0;
  ansi_fg_index = 7;
  gfx_fgcolor(COLOR_LTGRAY);
  gfx_set_cell_bg(0);
  gfx_setfont(2);  /* CP437 font for ANSI */
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
    } else if (p >= 40 && p <= 47) {
      /* Background color */
      gfx_set_cell_bg(ansi_color_map[p - 40]);
    } else if (p == 49) {
      /* Default background */
      gfx_set_cell_bg(0);
    }
  }
}


static void ansi_dispatch(unsigned char cmd) {
  int n, row, col, i;

  n = (param_count > 0 && params[0] > 0) ? params[0] : 1;

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
    if (n == 2) {
      gfx_cls();
      gfx_setcursxy(0, 0);
    }
    /* n=0 (clear to end) and n=1 (clear to start) — simplified: just clear whole screen */
    break;

  case 'K':  /* Erase in line */
    n = (param_count > 0) ? params[0] : 0;
    if (n == 0) {
      /* Clear from cursor to end of line */
      int save_x = gfx_cursx;
      int x;
      for (x = save_x; x < cfg_columns; x++) {
        gfx_setcursxy(x, gfx_cursy);
        gfx_draw_char(32);
      }
      gfx_setcursxy(save_x, gfx_cursy);
    } else if (n == 2) {
      gfx_clear_line(gfx_cursy, gfx_get_fgcolor());
    }
    break;

  case 'm':  /* SGR — Select Graphic Rendition */
    ansi_sgr();
    break;

  case 'n':  /* Device Status Report */
    if (param_count > 0 && params[0] == 6) {
      /* DSR: respond with cursor position ESC[row;colR (1-based) */
      char response[32];
      snprintf(response, sizeof(response), "\033[%d;%dR", gfx_cursy + 1, gfx_cursx + 1);
      net_send_string((const unsigned char *)response);
    }
    break;

  case 's':  /* Save cursor position */
    saved_x = gfx_cursx;
    saved_y = gfx_cursy;
    break;

  case 'u':  /* Restore cursor position */
    gfx_setcursxy(saved_x, saved_y);
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
      memset(params, 0, sizeof(params));
    } else {
      /* Unknown ESC sequence, discard */
      state = ANSI_NORMAL;
    }
    break;

  case ANSI_CSI:
    if (byte >= '0' && byte <= '9') {
      /* Accumulate digit */
      current_param = current_param * 10 + (byte - '0');
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
