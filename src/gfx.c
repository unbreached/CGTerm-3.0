#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "config.h"
#include "paths.h"
#include "cp437font.h"
#include "kernal.h"

unsigned char gfx_0400_buffer[80000];
unsigned char gfx_d800_buffer[80000];
unsigned char gfx_bg_buffer[80000];   /* per-cell background color (ANSI mode) */
unsigned char *gfx_0400 = gfx_0400_buffer;
unsigned char *gfx_d800 = gfx_d800_buffer;
unsigned char *gfx_bg = gfx_bg_buffer;
int gfx_offset;
int gfx_maxoffset;
int gfx_history_rows = 0;   /* rows scrolled off the top and still in the buffer */
signed int gfx_cursx, gfx_cursy;
int gfx_cursdirection;

static int gfx_menu_width;
static int gfx_menu_height;
static int gfx_menu_xpos;
static int gfx_menu_ypos;
static int gfx_menu_firstline;
static int gfx_menu_lastline;

static SDL_Surface *gfx_screen;
static int gfx_bpp;
/* Cursor blink: on and off for CURSOR_HALF_MS each. Timed, not counted per
 * gfx_vbl() call, so it no longer speeds up in loops that redraw more often
 * (transfers, dialogs). 333 ms matches the C64's 20-jiffy phase. */
#define CURSOR_HALF_MS 333
static Uint32 cursor_epoch = 0;   /* blink phase reference */
static int gfx_height, gfx_width;
static Uint8 charwidth, charheight;
static int borderleft, borderright;
static Uint8 colorundercursor;
static SDL_bool cursorvis;
static int cursor_hidden = 0;   /* gfx_cursor_show(0): no blink at all */

/* Optional status row drawn below the 25 terminal rows (cgterm only; cgchat
 * and cgedit keep the plain window). The text is kept as ASCII and converted
 * to the active font's codes at draw time. */
static int gfx_status_rows = 0;
static int gfx_desktop_w = 0;   /* desktop resolution, captured before the first */
static int gfx_desktop_h = 0;   /* SDL_SetVideoMode so we can clamp the zoom to it */
static char status_text[81];
static unsigned char status_color = 1;
static unsigned char status_bg = 0;
static int status_dirty = 0;
static void gfx_draw_status_row(void);
static void art_tick(int had_output);
static SDL_bool dirty[25];
static Uint8 fgcolor = 1, bgcolor = 0;
static int font;
static SDL_Surface *fontlist[3];
static SDL_Surface *rawfont[3];
static SDL_Color palette[] = {
  {0x00, 0x00, 0x00},
  {0xFF, 0xFF, 0xFF},
  {0x68, 0x37, 0x2B},
  {0x70, 0xA4, 0xB2},
  {0x6F, 0x3D, 0x86},
  {0x58, 0x8D, 0x43},
  {0x35, 0x28, 0x79},
  {0xB8, 0xC7, 0x6F},
  {0x6F, 0x4F, 0x25},
  {0x43, 0x39, 0x00},
  {0x9A, 0x67, 0x59},
  {0x44, 0x44, 0x44},
  {0x6C, 0x6C, 0x6C},
  {0x9A, 0xD2, 0x84},
  {0x6C, 0x5E, 0xB5},
  {0x95, 0x95, 0x95}
};
/* VGA/ANSI palette — same index order as C64 (BLACK=0, WHITE=1, RED=2, etc.)
 * but with standard VGA RGB values for proper ANSI color rendering */
/* VGA palette at C64 index positions — SyncTERM-matching RGB values.
 * Indices 8 and 12 repurposed for light magenta and light cyan
 * since ANSI needs those but C64 orange/gray are unused in ANSI mode. */
static SDL_Color vga_palette[] = {
  {0x00, 0x00, 0x00},  /* 0: BLACK */
  {0xFF, 0xFF, 0xFF},  /* 1: WHITE */
  {0xA8, 0x00, 0x00},  /* 2: RED */
  {0x00, 0xA8, 0xA8},  /* 3: CYAN */
  {0xA8, 0x00, 0xA8},  /* 4: MAGENTA */
  {0x00, 0xA8, 0x00},  /* 5: GREEN */
  {0x00, 0x00, 0xA8},  /* 6: BLUE */
  {0xFF, 0xFF, 0x54},  /* 7: YELLOW */
  {0xFF, 0x54, 0xFF},  /* 8: LIGHT MAGENTA (repurposed from orange) */
  {0xA8, 0x54, 0x00},  /* 9: BROWN */
  {0xFF, 0x54, 0x54},  /* 10: LIGHT RED */
  {0x54, 0x54, 0x54},  /* 11: DARK GRAY */
  {0x54, 0xFF, 0xFF},  /* 12: LIGHT CYAN (repurposed from gray) */
  {0x54, 0xFF, 0x54},  /* 13: LIGHT GREEN */
  {0x54, 0x54, 0xFF},  /* 14: LIGHT BLUE */
  {0xA8, 0xA8, 0xA8},  /* 15: LIGHT GRAY */
};
static unsigned char color_to_petscii[] = {
  0x90, 0x05, 0x1c, 0x9f,
  0x9c, 0x1e, 0x1f, 0x9e,
  0x81, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b
};


#define drawpixel(X, Y, C) \
  memcpy(((Uint8 *) surface->pixels) + surface->pitch*(Y) + surface->format->BytesPerPixel*(X), \
	 &(C), surface->format->BytesPerPixel)

SDL_Surface *gfx_createfont_pal(SDL_Surface *srcsurface, int zoom, SDL_Color *pal) {
  SDL_Surface *tempsurface;
  SDL_Surface *surface;
  Uint8 *sp;
  int c, x, y, z1, z2, col, hzoom;
  Uint32 fg, bg;

  /* The glyph reader assumes a 256-char, 32-col, 8x8 source (min 256x64).
   * A corrupt/undersized BMP would otherwise be read far out of bounds. */
  if (srcsurface == NULL || srcsurface->w < 256 || srcsurface->h < 64) {
    return(NULL);
  }

  if ((tempsurface = SDL_CreateRGBSurface(SDL_SWSURFACE, 256 * charwidth, 16 * charheight, 8, 0, 0, 0, 0)) == NULL) {
    return(NULL);
  }
  surface = SDL_DisplayFormat(tempsurface);
  SDL_FreeSurface(tempsurface);
  if (surface == NULL) {
    return(NULL);
  }
  SDL_SetPalette(surface, SDL_LOGPAL|SDL_PHYSPAL, pal, 0, 16);

  if (cfg_columns == 40) {
    hzoom = zoom;
  } else {
    hzoom = zoom / 2;
  }

  bg = SDL_MapRGB(surface->format, pal[bgcolor].r, pal[bgcolor].g, pal[bgcolor].b);
  for (col = 0; col < 16; ++col) {
    fg = SDL_MapRGB(surface->format, pal[col].r, pal[col].g, pal[col].b);
    for (c = 0; c < 256; ++c) {
      for (y = 0; y < 8; ++y) {
	for (z1 = 0; z1 < zoom; ++z1) {
	  sp = (Uint8 *)srcsurface->pixels + (c & 0x1f) * 8 + ((c / 32) * 8 + y) * srcsurface->pitch;
	  for (x = 0; x < 8; ++x) {
	    if (*sp) {
	      for (z2 = 0; z2 < hzoom; ++z2) {
		drawpixel(c * charwidth + x * hzoom + z2, col * charheight + y * zoom + z1, fg);
	      }
	    } else {
	      for (z2 = 0; z2 < hzoom; ++z2) {
		drawpixel(c * charwidth + x * hzoom + z2, col * charheight + y * zoom + z1, bg);
	      }
	    }
	    ++sp;
	  }
	}
      }
    }
  }

  return(surface);
}

/* ANSI font: uses magenta key color as transparent background.
 * Reads 16 rows per char from the source (8x16 CP437 font).
 * Rows are scaled to the cell height (8 * zoom). */
SDL_Surface *gfx_createfont_ansi(SDL_Surface *srcsurface, int zoom, SDL_Color *pal) {
  SDL_Surface *tempsurface;
  SDL_Surface *surface;
  Uint8 *sp;
  int c, x, y, z2, col, hzoom;
  Uint32 fg, keycol;
  int src_char_h = 16;  /* source is 8x16 */

  /* Reader assumes a 256-char, 32-col, 8x16 source (min 256x128). */
  if (srcsurface == NULL || srcsurface->w < 256 || srcsurface->h < 128) {
    return(NULL);
  }

  if ((tempsurface = SDL_CreateRGBSurface(SDL_SWSURFACE, 256 * charwidth, 16 * charheight, 8, 0, 0, 0, 0)) == NULL) {
    return(NULL);
  }
  surface = SDL_DisplayFormat(tempsurface);
  SDL_FreeSurface(tempsurface);
  if (surface == NULL) {
    return(NULL);
  }

  if (cfg_columns == 40) {
    hzoom = zoom;
  } else {
    hzoom = zoom / 2;
  }

  /* Use bright magenta as transparent key color (not in VGA palette) */
  keycol = SDL_MapRGB(surface->format, 0xFF, 0x00, 0xFF);
  SDL_FillRect(surface, NULL, keycol);

  for (col = 0; col < 16; ++col) {
    fg = SDL_MapRGB(surface->format, pal[col].r, pal[col].g, pal[col].b);
    for (c = 0; c < 256; ++c) {
      /* Scale the 16 source rows to the full cell height. This used to
       * copy them 1:1, which is only right at zoom 2 (16 px cells): at
       * zoom 4 the glyphs filled the top half of each cell and box
       * drawing no longer joined vertically. */
      for (y = 0; y < charheight; ++y) {
	  int sy = y * src_char_h / charheight;
	  sp = (Uint8 *)srcsurface->pixels + (c & 0x1f) * 8 + ((c / 32) * src_char_h + sy) * srcsurface->pitch;
	  for (x = 0; x < 8; ++x) {
	    if (*sp) {
	      for (z2 = 0; z2 < hzoom; ++z2) {
		drawpixel(c * charwidth + x * hzoom + z2, col * charheight + y, fg);
	      }
	    }
	    /* else: leave as keycol (transparent) */
	    ++sp;
	  }
      }
    }
  }

  SDL_SetColorKey(surface, SDL_SRCCOLORKEY, keycol);
  return(surface);
}

SDL_Surface *gfx_createfont(SDL_Surface *srcsurface, int zoom) {
  return gfx_createfont_pal(srcsurface, zoom, palette);
}


void gfx_destroyfont(SDL_Surface *fontsurface) {
  SDL_FreeSurface(fontsurface);
}


SDL_Surface *gfx_loadfont(char *fontname) {
  SDL_Surface *surface;

  if ((surface = SDL_LoadBMP(fontname)) == NULL) {
    printf("Unable to load font %s %s\n", fontname, SDL_GetError());
    return(NULL);
  }

  return(surface);
}


/* Largest integer zoom (1..8) whose window fits on the desktop, leaving a
 * little headroom for the title bar and taskbar/panel. Guards against a config
 * (or a window drag) asking for a window bigger than the screen, which the WM
 * then clips to a useless black rectangle (GitHub issue #12). When the desktop
 * size is unknown we don't restrict anything. */
int gfx_max_zoom(void) {
  int z, avail_w, avail_h;
  /* The font atlas is 256 glyphs wide. In 40 columns at zoom 8 that is
   * 16384 px, which SDL refuses, leaving NULL fonts and a blank window (and
   * a saved zoom that failed at the next start). 7 is the 40-column limit. */
  int top = (cfg_columns == 40) ? 7 : 8;
  if (gfx_desktop_w <= 0 || gfx_desktop_h <= 0) return top;
  avail_w = gfx_desktop_w;
  avail_h = gfx_desktop_h - 80;   /* title bar + taskbar/menu-bar headroom */
  for (z = top; z > 1; --z) {
    if (GFX_WIDTH * z <= avail_w && (GFX_HEIGHT + 8) * z <= avail_h) return z;
  }
  return 1;
}

/* Clamp a requested zoom to something that is both displayable and valid for
 * the current column mode (80 columns needs an even zoom). */
static int gfx_clamp_zoom(int z) {
  int mx = gfx_max_zoom();
  if (z > mx) z = mx;
  if (z < 1) z = 1;
  if (cfg_columns == 80 && (z & 1)) { z -= 1; if (z < 2) z = 2; }
  return z;
}


int gfx_init(int fullscreen, char *appname) {
  const SDL_VideoInfo *vidinfo;
  char fname[1024];
    
  if (cfg_columns == 40) {
    charwidth = 8 * cfg_zoom;
  } else {
    if (cfg_zoom == 1 || cfg_zoom == 3) {
      printf("Warning: must use zoom 2 or 4 for 80 column mode\n");
      cfg_zoom = 2;
    }
    charwidth = 4 * cfg_zoom;
  }
  charheight = 8 * cfg_zoom;
  gfx_width = cfg_zoom * GFX_WIDTH;
  gfx_height = cfg_zoom * GFX_HEIGHT;
  borderleft = 0;
  borderright = 0;

  gfx_offset = gfx_maxoffset = sizeof(gfx_0400_buffer) - cfg_columns * 25;
  gfx_history_rows = 0;   /* old rows are wrapped for the previous width */
  gfx_0400 = gfx_0400_buffer + gfx_maxoffset;
  gfx_d800 = gfx_d800_buffer + gfx_maxoffset;
  gfx_bg = gfx_bg_buffer + gfx_maxoffset;

  /* Pick the scaling filter sdl12-compat uses when it stretches our surface to
   * the window: linear (smooth) in fill mode, nearest (crisp pixels) otherwise.
   * Read at SDL_Init, so a change takes effect on the next launch. (issue #1) */
#ifdef WINDOWS
  _putenv(cfg_smoothscale ? "SDL12COMPAT_SCALE_METHOD=linear"
                          : "SDL12COMPAT_SCALE_METHOD=nearest");
#else
  setenv("SDL12COMPAT_SCALE_METHOD", cfg_smoothscale ? "linear" : "nearest", 1);
#endif

  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0) {
    printf("Unable to init SDL: %s\n", SDL_GetError());
    return(1);
  }
  atexit(SDL_Quit);
  vidinfo = SDL_GetVideoInfo();
  if ((gfx_bpp = vidinfo->vfmt->BitsPerPixel) < 15) {
    gfx_bpp = 16;
  }
  /* current_w/current_h are the desktop resolution here, because this is
   * before the first SDL_SetVideoMode. Remember it so the zoom can be clamped
   * to a window that actually fits on screen. */
  if (vidinfo->current_w > 0 && vidinfo->current_h > 0) {
    gfx_desktop_w = vidinfo->current_w;
    gfx_desktop_h = vidinfo->current_h;
  }
  /* A stale or hand-edited config can carry a zoom too large for this display,
   * which opens as an unusable black window (issue #12). Clamp it, and persist
   * the corrected value so the config stops fighting us. */
  if (cfg_columns == 40 && cfg_zoom > 7) {
    cfg_zoom = 7;                 /* atlas limit, fullscreen included */
    charwidth = 8 * cfg_zoom;
    charheight = 8 * cfg_zoom;
    gfx_width = cfg_zoom * GFX_WIDTH;
    gfx_height = cfg_zoom * GFX_HEIGHT;
  }
  if (!fullscreen) {
    int clamped = gfx_clamp_zoom(cfg_zoom);
    if (clamped != cfg_zoom) {
      char val[8];
      cfg_zoom = clamped;
      charwidth = (cfg_columns == 40 ? 8 : 4) * cfg_zoom;
      charheight = 8 * cfg_zoom;
      gfx_width = cfg_zoom * GFX_WIDTH;
      gfx_height = cfg_zoom * GFX_HEIGHT;
      snprintf(val, sizeof(val), "%d", cfg_zoom);
      cfg_save_setting("zoom", val);
    }
  }
  if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp, (SDL_FULLSCREEN * fullscreen)|(fullscreen ? 0 : SDL_RESIZABLE)|SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
    printf("Unable to open window: %s\n", SDL_GetError());
    return(1);
  }
  SDL_WM_SetCaption(appname, appname);
  SDL_ShowCursor(SDL_DISABLE);  /* keyboard-driven terminal: hide the OS mouse cursor */
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

  /* Load C64 character ROM fonts based on charset setting */
  {
    const char *upper_name = "upper.bmp";
    const char *lower_name = "lower.bmp";
    if (cfg_charset == 1) {
      upper_name = "upper-swedish.bmp";
      lower_name = "lower-swedish.bmp";
    } else if (cfg_charset == 2) {
      upper_name = "upper-german.bmp";
      lower_name = "lower-german.bmp";
    }
    path_build_asset(fname, sizeof(fname), upper_name);
    if ((rawfont[0] = gfx_loadfont(fname)) == NULL) {
      /* Fall back to standard */
      path_build_asset(fname, sizeof(fname), "upper.bmp");
      rawfont[0] = gfx_loadfont(fname);
    }
    if (rawfont[0] == NULL) { printf("Error: upper font\n"); return(1); }
    path_build_asset(fname, sizeof(fname), lower_name);
    if ((rawfont[1] = gfx_loadfont(fname)) == NULL) {
      path_build_asset(fname, sizeof(fname), "lower.bmp");
      rawfont[1] = gfx_loadfont(fname);
    }
    if (rawfont[1] == NULL) { printf("Error: lower font\n"); return(1); }
  }
  rawfont[2] = cp437_create_raw_font();
  if (
      (fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom)) == NULL ||
      (fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom)) == NULL ||
      rawfont[2] == NULL ||
      (fontlist[2] = gfx_createfont_ansi(rawfont[2], cfg_zoom, vga_palette)) == NULL
      ) {
        printf("gfx_createfont Error\n");
    return(1);
  }
  font = 1;
  memset(dirty, SDL_FALSE, sizeof(dirty));

  if (menu_init(gfx_width, gfx_height)) {
    printf("menu_init Error\n");
    return(1);
  }
  gfx_menu_width = gfx_width;
  gfx_menu_height = gfx_height;
  gfx_menu_xpos = 0;
  gfx_menu_ypos = 0;
  gfx_menu_firstline = 0;
  gfx_menu_lastline = (gfx_height - 1) / charheight;

  cursor_epoch = SDL_GetTicks();
  cursorvis = SDL_FALSE;

  gfx_cursdirection = 1;

  return(0);
}


/* Set while the blink phase is "on" in ANSI mode, where the cursor is
 * drawn as a block over the cell at blit time (see gfx_vbl). */
static SDL_bool ansi_cursor_on = SDL_FALSE;

void invertcursor(void) {
  if (cfg_termmode == 1) {
    /* XOR 0x80 means reverse video only in PETSCII screen codes. The ANSI
     * font is indexed by CP437 byte, so flipping bit 7 showed a different
     * character (a space blinked as an accented letter). */
    ansi_cursor_on = cursorvis ? SDL_FALSE : SDL_TRUE;   /* called just before cursorvis flips */
  } else {
    gfx_0400[gfx_cursy * cfg_columns + gfx_cursx] ^= 0x80;
  }
  dirty[gfx_cursy] = SDL_TRUE;
}


void resetcursor(void) {
  if (cursorvis) {
    invertcursor();
    cursorvis = SDL_FALSE;
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = colorundercursor;
  }
  cursor_epoch = SDL_GetTicks() - CURSOR_HALF_MS;   /* show again on the next frame */
}


void gfx_setfont(int f) {
  if (font != f) {
    font = f;
    memset(dirty, SDL_TRUE, sizeof(dirty));
  }
}


void gfx_toggle_font(void) {
  font ^= 1;
  memset(dirty, SDL_TRUE, sizeof(dirty));
  cfg_save_setting("case", font ? "lower" : "upper");
}

int gfx_get_font(void) {
  return font;
}


void gfx_bgcolor(int c) {
  resetcursor();
  if (bgcolor != c) {
    memset(dirty, SDL_TRUE, sizeof(dirty));
    bgcolor = c;
    gfx_destroyfont(fontlist[0]);
    gfx_destroyfont(fontlist[1]);
    gfx_destroyfont(fontlist[2]);
    fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom);
    fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom);
    fontlist[2] = gfx_createfont_ansi(rawfont[2], cfg_zoom, vga_palette);
  }
}


void gfx_fgcolor(int c) {
  resetcursor();
  fgcolor = c;
}

int gfx_get_fgcolor(void) {
  return fgcolor;
}

static Uint8 ansi_cell_bg = 0;  /* per-cell BG color for ANSI mode */

void gfx_set_cell_bg(int c) {
  ansi_cell_bg = c;
}


void gfx_draw_char(int c) {
  resetcursor();
  gfx_0400[gfx_cursy * cfg_columns + gfx_cursx] = c;
  gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = fgcolor;
  gfx_bg[gfx_cursy * cfg_columns + gfx_cursx] = ansi_cell_bg;
  dirty[gfx_cursy] = SDL_TRUE;
}


void gfx_togglerev(void) {
  resetcursor();
  gfx_0400[gfx_cursy * cfg_columns + gfx_cursx] ^= 0x80;
  dirty[gfx_cursy] = SDL_TRUE;
}


void gfx_updatecolor(void) {
  resetcursor();
  gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = fgcolor;
  dirty[gfx_cursy] = SDL_TRUE;
}


void gfx_clear_line(int line, int color) {
  int offset;

  dirty[line] = SDL_TRUE;
  offset = cfg_columns * line;
  memset(gfx_0400 + offset, ' ', cfg_columns);
  memset(gfx_d800 + offset, color, cfg_columns);
  memset(gfx_bg + offset, 0, cfg_columns);
}


void gfx_copy_line(unsigned char *chars, unsigned char *colors, int line) {
  unsigned char *screen, *colram;
  int offset, i;

  resetcursor();
  dirty[line] = SDL_TRUE;
  offset = cfg_columns * line;
  memset(gfx_0400 + offset, ' ', cfg_columns);
  screen = gfx_0400 + offset;
  colram = gfx_d800 + offset;
  i = cfg_columns;
  while (i--) {
    *screen++ = *chars++;
    *colram++ = *colors++;
  }
}


/* Move one on-screen line to another, including the per-cell background-color
 * plane (gfx_bg). Used by the ANSI insert/delete/scroll-line handlers; without
 * scrolling gfx_bg the ANSI background colors would be left behind. */
void gfx_scroll_line(int srcline, int dstline) {
  int srcoff = cfg_columns * srcline;
  int dstoff = cfg_columns * dstline;

  resetcursor();
  dirty[dstline] = SDL_TRUE;
  memmove(gfx_0400 + dstoff, gfx_0400 + srcoff, cfg_columns);
  memmove(gfx_d800 + dstoff, gfx_d800 + srcoff, cfg_columns);
  memmove(gfx_bg + dstoff, gfx_bg + srcoff, cfg_columns);
}


void gfx_cls(void) {
  resetcursor();   /* otherwise the blink phase XORs a stray 0x80 into the blank cell */
  memset(gfx_0400, 32, cfg_columns * 25);
  memset(gfx_d800, fgcolor, cfg_columns * 25);
  memset(gfx_bg, 0, cfg_columns * 25);
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


void gfx_scrollup(void) {
  resetcursor();   /* don't scroll an inverted cursor glyph up into the text */
  if (gfx_history_rows < gfx_maxoffset / cfg_columns) ++gfx_history_rows;
  memmove(gfx_0400_buffer, gfx_0400_buffer + cfg_columns, sizeof(gfx_0400_buffer) - (26 - cfg_rows) * cfg_columns);
  memmove(gfx_d800_buffer, gfx_d800_buffer + cfg_columns, sizeof(gfx_d800_buffer) - (26 - cfg_rows) * cfg_columns);
  memmove(gfx_bg_buffer, gfx_bg_buffer + cfg_columns, sizeof(gfx_bg_buffer) - (26 - cfg_rows) * cfg_columns);
  memset(gfx_0400 + (cfg_rows - 1) * cfg_columns, 32, cfg_columns);
  memset(gfx_d800 + (cfg_rows - 1) * cfg_columns, fgcolor, cfg_columns);
  memset(gfx_bg + (cfg_rows - 1) * cfg_columns, 0, cfg_columns);
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


void gfx_conv_screen_to_pet(unsigned char *chars, unsigned char *colors, unsigned char *petsciibuf, int *lastcolor, int *reverse, int addcr, int width) {
  int column, empty;
  int i, c;

  for (column = 0; column < width; ++column) {
    empty = 1;
    for (i = column; i < width; ++i) {
      if (chars[i] != 32) {
	empty = 0;
	i = width;
      }
    }
    if (empty) {
      if (addcr) {
	*petsciibuf++ = 13;
      }
      *reverse = 0;
      column = width;
    } else {
      i = column;
      if (chars[i] > 127 && *reverse == 0) {
	*petsciibuf++ = 18;
	*reverse = 1;
      } else if (chars[i] < 128 && *reverse == 1) {
	*petsciibuf++ = 146;
	*reverse = 0;
      }
      if (chars[i] != 32 && *lastcolor != colors[i]) {
	*lastcolor = colors[i];
	*petsciibuf++ = color_to_petscii[*lastcolor];
      }
      c = chars[i] & 0x7f;
      switch (c / 32) {
      case 1:
	break;
      case 2:
	c += 32;
	break;
      default:
	c += 64;
	break;
      }
      *petsciibuf++ = c;
    }
  }
  *petsciibuf = 0;
}


void gfx_savescreen(char *filename) {
  unsigned char converted[256 * 3 + 1];   /* room for any column count */
  FILE *f_screen;
  int row, lastcolor = 256, reverse = 0;

  resetcursor();

  /* If filename contains a path separator, use it as-is.
   * Otherwise prepend home/Downloads. */
  {
    char fpath[1024];
    if (strchr(filename, '/') || strchr(filename, '\\')) {
      snprintf(fpath, sizeof(fpath), "%s", filename);
    } else {
      snprintf(fpath, sizeof(fpath), "%s/Downloads/%s", cfg_homedir, filename);
    }

    if ((f_screen = fopen(fpath, "wb"))) {
      for (row = 0; row < 25; ++row) {
        gfx_conv_screen_to_pet(gfx_0400_buffer + gfx_offset + row * cfg_columns,
          gfx_d800_buffer + gfx_offset + row * cfg_columns,
          converted, &lastcolor, &reverse, row == 24 ? 0 : 1, cfg_columns);
        fputs((const char *)converted, f_screen);
      }
      fclose(f_screen);
    } else {
      printf("Couldn't open %s for writing\n", fpath);
    }
  }
}


void gfx_draw_line(int ypos) {
  int xpos;
  SDL_Rect src, dest;

  src.w = charwidth;
  src.h = charheight;
  dest.w = charwidth;
  dest.h = charheight;
  for (xpos = 0; xpos < cfg_columns; ++xpos) {
    src.x = gfx_0400_buffer[gfx_offset + ypos * cfg_columns + xpos] * charwidth;
    src.y = gfx_d800_buffer[gfx_offset + ypos * cfg_columns + xpos] * charheight;
    dest.x = xpos * charwidth;
    dest.y = ypos * charheight;
    /* ANSI mode (font 2): fill cell with per-cell BG color first */
    if (font == 2) {
      unsigned char bgc = gfx_bg_buffer[gfx_offset + ypos * cfg_columns + xpos];
      SDL_FillRect(gfx_screen, &dest, SDL_MapRGB(gfx_screen->format,
        vga_palette[bgc].r, vga_palette[bgc].g, vga_palette[bgc].b));
    }
    SDL_BlitSurface(fontlist[font], &src, gfx_screen, &dest);
  }
  if (ansi_cursor_on && font == 2 && ypos == gfx_cursy && gfx_offset == gfx_maxoffset) {
    /* ANSI cursor: an underline bar, like a DOS text cursor (drawn before
     * the menu overlay so a menu always covers it) */
    SDL_Rect cr;
    int ch = charheight / 8;
    if (ch < 2) ch = 2;
    cr.x = gfx_cursx * charwidth;
    cr.y = gfx_cursy * charheight + charheight - ch;
    cr.w = charwidth;
    cr.h = ch;
    SDL_FillRect(gfx_screen, &cr, SDL_MapRGB(gfx_screen->format, 0xAA, 0xAA, 0xAA));
  }
  if (menu_visible && ypos >= gfx_menu_firstline && ypos <= gfx_menu_lastline) {
    src.w = gfx_menu_width;
    src.h = charheight;
    src.x = 0;
    src.y = ypos * charheight - gfx_menu_ypos;
    dest.x = gfx_menu_xpos;
    dest.y = ypos * charheight;
    /* menu_composite is the window-sized, scaled copy of the design surface */
    SDL_BlitSurface(menu_composite, &src, gfx_screen, &dest);
  }
}


/* ASCII -> screen code for the PETSCII fonts (font 0 = upper/graphics,
 * font 1 = lower/upper). Letters land on 1-26 / 65-90, punctuation and
 * digits keep their code, everything else becomes a space. */
static unsigned char ascii_to_screencode(unsigned char c, int fnt) {
  if (fnt == 2) return c;                       /* CP437 is byte-ordered */
  if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 1);
  if (c >= 'A' && c <= 'Z') return (unsigned char)(fnt == 1 ? c - 'A' + 65 : c - 'A' + 1);
  if (c == '@') return 0;
  if (c == '[') return 27;
  if (c == ']') return 29;
  if (c >= 0x20 && c <= 0x3F) return c;
  if (c == '|') return 93;                      /* vertical bar graphic */
  if (c == '_') return 100;
  return 32;
}

/* Screen code -> ASCII (inverse of the above, for copy-to-clipboard). */
static char screencode_to_ascii(unsigned char sc, int fnt) {
  if (fnt == 2) {
    /* CP437 is byte-ordered and bit 7 is part of the character: masking it
     * first turned box drawing into letters (0xC4 came out as 'D'). */
    if (sc >= 0x20 && sc < 0x7F) return (char)sc;
    if (sc == 0xB3 || sc == 0xBA) return '|';
    if (sc == 0xC4 || sc == 0xCD) return '-';
    if (sc >= 0xB0 && sc <= 0xDF) return (sc >= 0xB0 && sc <= 0xB2) || sc >= 0xDB ? '#' : '+';
    return ' ';
  }
  sc &= 0x7F;
  if (sc == 0) return '@';
  if (sc >= 1 && sc <= 26) return (char)(fnt == 1 ? 'a' + sc - 1 : 'A' + sc - 1);
  if (sc == 27) return '[';
  if (sc == 28) return '#';
  if (sc == 29) return ']';
  if (sc == 30) return '^';
  if (sc == 31) return '-';
  if (sc >= 0x20 && sc <= 0x3F) return (char)sc;
  if (fnt == 1 && sc >= 65 && sc <= 90) return (char)('A' + sc - 65);
  if (sc == 100) return '_';
  if (sc == 93) return '|';
  return ' ';
}

void gfx_status_enable(int on) {
  gfx_status_rows = on ? 1 : 0;
}

int gfx_status_enabled(void) {
  return gfx_status_rows;
}

void gfx_status_set(const char *text, unsigned char fg, unsigned char bg) {
  char buf[81];
  snprintf(buf, sizeof(buf), "%-*.*s", cfg_columns, cfg_columns, text ? text : "");
  if (strcmp(buf, status_text) != 0 || fg != status_color || bg != status_bg) {
    memcpy(status_text, buf, sizeof(status_text));
    status_color = fg;
    status_bg = bg;
    status_dirty = 1;
  }
}

static void gfx_draw_status_row(void) {
  int xpos;
  SDL_Rect src, dest;

  src.w = charwidth;
  src.h = charheight;
  dest.w = charwidth;
  dest.h = charheight;
  dest.y = 25 * charheight;
  for (xpos = 0; xpos < cfg_columns; ++xpos) {
    unsigned char sc = ascii_to_screencode((unsigned char)status_text[xpos], font);
    if (font != 2) sc |= 0x80;                   /* reverse video bar in PETSCII */
    src.x = sc * charwidth;
    src.y = status_color * charheight;
    dest.x = xpos * charwidth;
    if (font == 2) {
      SDL_FillRect(gfx_screen, &dest, SDL_MapRGB(gfx_screen->format,
        vga_palette[status_bg].r, vga_palette[status_bg].g, vga_palette[status_bg].b));
    }
    SDL_BlitSurface(fontlist[font], &src, gfx_screen, &dest);
  }
  SDL_UpdateRect(gfx_screen, 0, 25 * charheight, gfx_width, charheight);
}

/* Visible screen as text lines (trailing blanks trimmed), for the
 * clipboard. Returns the number of bytes written excluding the NUL. */
int gfx_screen_to_text(char *out, size_t outsz) {
  int y, x;
  size_t n = 0;

  if (!out || outsz == 0) return 0;
  for (y = 0; y < 25 && n + 2 < outsz; y++) {
    int last = -1;
    char line[81];
    for (x = 0; x < cfg_columns && x < 80; x++) {
      line[x] = screencode_to_ascii(gfx_0400[y * cfg_columns + x], font);
      if (line[x] != ' ') last = x;
    }
    for (x = 0; x <= last && n + 2 < outsz; x++) out[n++] = line[x];
    out[n++] = '\n';
  }
  out[n] = 0;
  return (int)n;
}

void gfx_vbl(void) {
  int first, last, l;

  int blink_on = (((SDL_GetTicks() - cursor_epoch) / CURSOR_HALF_MS) & 1) != 0;

  if (cursor_hidden) {
    /* ESC[?25l, splash, background image: no blink at all */
  } else if (blink_on && !cursorvis) {
    invertcursor();
    cursorvis = SDL_TRUE;
    colorundercursor = gfx_d800[gfx_cursy * cfg_columns + gfx_cursx];
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = fgcolor;
  } else if (!blink_on && cursorvis) {
    invertcursor();
    cursorvis = SDL_FALSE;
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = colorundercursor;
  }

  if (menu_dirty) {
    menu_dirty = SDL_FALSE;
    menu_update_composite();   /* scale the freshly-drawn menu to the window (issue #6) */
    for (l = gfx_menu_firstline; l <= gfx_menu_lastline; ++l) {
      dirty[l] = SDL_TRUE;
    }
  }

  first = 25;
  last = -1;
  for (l = 0; l < 25; ++l) {
    if (dirty[l]) {
      gfx_draw_line(l);
      dirty[l] = SDL_FALSE;
      if (l < first) {
	first = l;
      }
      last = l;
    }
  }
  if (first != 25) {
    SDL_UpdateRect(gfx_screen, 0, first * charheight, gfx_width, (last - first) * charheight + charheight);
  }
  if (gfx_status_rows && (status_dirty || first != 25)) {
    gfx_draw_status_row();
    status_dirty = 0;
  }
  art_tick(first != 25);

}


void gfx_cursor_show(int show) {
  if (!show) {
    resetcursor();
  }
  cursor_hidden = !show;
}


void gfx_setcursxy(int x, int y) {
  resetcursor();
  /* clamp to the visible buffer (cfg_columns x 25 rows): the cursor index
     gfx_cursy * cfg_columns + gfx_cursx feeds every screen write, so this is
     the last line of defence against an out-of-range cursor from any caller. */
  if (x < 0) x = 0; else if (x >= cfg_columns) x = cfg_columns - 1;
  if (y < 0) y = 0; else if (y > 24) y = 24;
  gfx_cursx = x;
  gfx_cursy = y;
}


void gfx_cursleft(void) {
  resetcursor();
  if (gfx_cursx || gfx_cursy) {
    if (--gfx_cursx < 0) {
      gfx_cursx = cfg_columns - 1;
      --gfx_cursy;
    }
  }
}


void gfx_cursright(void) {
  resetcursor();
  if (++gfx_cursx > cfg_columns - 1) {
    if (cfg_rows == 24) { // this is a hack, fixme
      gfx_cursx = 2;
    } else {
      gfx_cursx = 0;
    }
    gfx_cursdown();
  }
}


void gfx_cursup(void) {
  resetcursor();
  if (gfx_cursy) {
    --gfx_cursy;
  }
}


void gfx_cursdown(void) {
  resetcursor();
  if (++gfx_cursy > cfg_rows - 1) {
    gfx_cursy = cfg_rows - 1;
    gfx_scrollup();
  }
}


void gfx_cursadvance(void) {
  switch (gfx_cursdirection) {
  case 0:
    break;
  case 1:
    gfx_cursright();
    break;
  case 2:
    gfx_cursdown();
    break;
  case 3:
    gfx_cursleft();
    break;
  case 4:
    gfx_cursup();
    break;
  }
}


void gfx_delete(void) {
  int x;
  unsigned char *rowchar = &gfx_0400[gfx_cursy * cfg_columns];
  unsigned char *rowcolor = &gfx_d800[gfx_cursy * cfg_columns];
  unsigned char *rowbg = &gfx_bg[gfx_cursy * cfg_columns];

  resetcursor();
  if (gfx_cursx) {
    for (x = gfx_cursx - 1; x < cfg_columns - 1; ++x) {
      rowchar[x] = rowchar[x + 1];
      rowcolor[x] = rowcolor[x + 1];
      rowbg[x] = rowbg[x + 1];
    }
    rowchar[x] = 32;
    rowcolor[x] = fgcolor;
    rowbg[x] = 0;
    --gfx_cursx;
    dirty[gfx_cursy] = SDL_TRUE;
  } else {
    if (gfx_cursy) {
      gfx_cursx = cfg_columns - 1;
      --gfx_cursy;
      gfx_draw_char(32);
    }
    dirty[gfx_cursy] = SDL_TRUE;
  }
}


void gfx_insert(void) {
  int x;
  unsigned char *rowchar = &gfx_0400[gfx_cursy * cfg_columns];
  unsigned char *rowcolor = &gfx_d800[gfx_cursy * cfg_columns];
  unsigned char *rowbg = &gfx_bg[gfx_cursy * cfg_columns];

  resetcursor();
  if (gfx_cursx < cfg_columns - 1) {
    if (rowchar[cfg_columns - 1] == 32) {
      for (x = cfg_columns - 1; x > gfx_cursx; --x) {
	rowchar[x] = rowchar[x - 1];
	rowcolor[x] = rowcolor[x - 1];
	rowbg[x] = rowbg[x - 1];
      }
      rowchar[x] = 32;
      rowcolor[x] = fgcolor;
      rowbg[x] = 0;
      dirty[gfx_cursy] = SDL_TRUE;
    }
  }
}


void gfx_set_title(const char *title) {
  SDL_WM_SetCaption(title, title);
}


void gfx_toggle_fullscreen(void) {
  //SDL_WM_ToggleFullScreen(gfx_screen);
  if (cfg_fullscreen) {
    SDL_Surface *ns = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp, SDL_RESIZABLE|SDL_ANYFORMAT|SDL_SWSURFACE);
    if (ns == NULL) {
      /* keep the current mode: exiting here dropped the connection */
      printf("Unable to open window: %s\n", SDL_GetError());
      return;
    }
    gfx_screen = ns;
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(SDL_DISABLE);  /* stay hidden in windowed mode too */
    cfg_fullscreen = 0;
  } else {
    SDL_Surface *ns = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp, SDL_FULLSCREEN|SDL_ANYFORMAT|SDL_SWSURFACE);
    if (ns == NULL) {
      printf("Unable to go fullscreen: %s\n", SDL_GetError());
      return;
    }
    gfx_screen = ns;
    cfg_fullscreen = 1;
  }
  /* Regenerate fonts — SDL_SetVideoMode may invalidate surfaces on some platforms */
  gfx_destroyfont(fontlist[0]);
  gfx_destroyfont(fontlist[1]);
  gfx_destroyfont(fontlist[2]);
  fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom);
  fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom);
  fontlist[2] = gfx_createfont_ansi(rawfont[2], cfg_zoom, vga_palette);
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


void gfx_reload_charset(void) {
  char fname[1024];
  const char *upper_name = "upper.bmp";
  const char *lower_name = "lower.bmp";
  if (cfg_charset == 1) {
    upper_name = "upper-swedish.bmp";
    lower_name = "lower-swedish.bmp";
  } else if (cfg_charset == 2) {
    upper_name = "upper-german.bmp";
    lower_name = "lower-german.bmp";
  }
  path_build_asset(fname, sizeof(fname), upper_name);
  if (rawfont[0]) SDL_FreeSurface(rawfont[0]);
  rawfont[0] = gfx_loadfont(fname);
  if (!rawfont[0]) {
    path_build_asset(fname, sizeof(fname), "upper.bmp");
    rawfont[0] = gfx_loadfont(fname);
  }
  path_build_asset(fname, sizeof(fname), lower_name);
  if (rawfont[1]) SDL_FreeSurface(rawfont[1]);
  rawfont[1] = gfx_loadfont(fname);
  if (!rawfont[1]) {
    path_build_asset(fname, sizeof(fname), "lower.bmp");
    rawfont[1] = gfx_loadfont(fname);
  }
  gfx_destroyfont(fontlist[0]);
  gfx_destroyfont(fontlist[1]);
  fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom);
  fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom);
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


static int saved_zoom = 0;  /* remember zoom before 80-col switch */

void gfx_set_columns(int cols) {
  int old_columns = cfg_columns, old_zoom = cfg_zoom, old_saved_zoom = saved_zoom;
  int old_charwidth = charwidth, old_charheight = charheight;
  int old_width = gfx_width, old_height = gfx_height;

  if (cols != 40 && cols != 80) return;
  if (cols == cfg_columns) return;

  /* Compute the new geometry first; nothing is persisted until the video
   * mode switch has succeeded, otherwise a failed SDL_SetVideoMode left
   * cfg_columns/charwidth (and the saved config) describing a layout the
   * screen buffers were never re-pointed for. */
  cfg_columns = cols;
  if (cfg_columns == 40) {
    /* Restore original zoom when going back to 40 cols */
    if (saved_zoom > 0) {
      cfg_zoom = saved_zoom;
      saved_zoom = 0;
    }
    if (cfg_zoom > 7) cfg_zoom = 7;   /* 40-column font atlas limit */
    charwidth = 8 * cfg_zoom;
    charheight = 8 * cfg_zoom;   /* must track cfg_zoom, or the regenerated
                                  * font surface and dirty[] overflow */
  } else {
    /* Save zoom before forcing it for 80 cols */
    if (cfg_zoom == 1 || cfg_zoom == 3) {
      saved_zoom = cfg_zoom;
      cfg_zoom = 2;
    }
    charwidth = 4 * cfg_zoom;
    charheight = 8 * cfg_zoom;   /* see above: forcing zoom 1/3 -> 2 here
                                  * previously left charheight stale at 8 */
  }
  gfx_width = cfg_zoom * GFX_WIDTH;
  gfx_height = cfg_zoom * GFX_HEIGHT;

  {
    SDL_Surface *newscreen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp,
        (SDL_FULLSCREEN * cfg_fullscreen)|(cfg_fullscreen ? 0 : SDL_RESIZABLE)|SDL_ANYFORMAT|SDL_SWSURFACE);
    if (newscreen == NULL) {
      /* keep the existing surface and layout rather than leaving gfx_screen NULL */
      printf("Unable to resize window: %s\n", SDL_GetError());
      cfg_columns = old_columns;
      cfg_zoom = old_zoom;
      saved_zoom = old_saved_zoom;
      charwidth = old_charwidth;
      charheight = old_charheight;
      gfx_width = old_width;
      gfx_height = old_height;
      return;
    }
    gfx_screen = newscreen;
  }

  {
    char val[8];
    snprintf(val, sizeof(val), "%d", cfg_columns);
    cfg_save_setting("columns", val);
    if (cfg_zoom != old_zoom) {
      snprintf(val, sizeof(val), "%d", cfg_zoom);
      cfg_save_setting("zoom", val);
    }
  }

  /* Regenerate fonts for new charwidth */
  gfx_destroyfont(fontlist[0]);
  gfx_destroyfont(fontlist[1]);
  gfx_destroyfont(fontlist[2]);
  fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom);
  fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom);
  fontlist[2] = gfx_createfont_ansi(rawfont[2], cfg_zoom, vga_palette);

  /* Reset buffer pointers */
  gfx_offset = gfx_maxoffset = sizeof(gfx_0400_buffer) - cfg_columns * 25;
  gfx_history_rows = 0;   /* old rows are wrapped for the previous width */
  gfx_0400 = gfx_0400_buffer + gfx_maxoffset;
  gfx_d800 = gfx_d800_buffer + gfx_maxoffset;
  gfx_bg = gfx_bg_buffer + gfx_maxoffset;

  /* Resize menu overlay */
  if (menu_init(gfx_width, gfx_height)) {
    printf("menu_init failed on column switch: %s\n", SDL_GetError());
    return;
  }
  gfx_menu_width = gfx_width;
  gfx_menu_height = gfx_height;
  gfx_menu_lastline = (gfx_height - 1) / charheight;

  /* Clear screen surface to black — prevents leftover pixels from previous mode */
  SDL_FillRect(gfx_screen, NULL, SDL_MapRGB(gfx_screen->format, 0, 0, 0));

  /* Clear character buffers and BG buffer */
  gfx_setcursxy(0, 0);
  gfx_cls();
  memset(gfx_bg_buffer, 0, sizeof(gfx_bg_buffer));
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


void gfx_set_offset(int offset) {
  gfx_offset = offset;
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


/* Runtime zoom change (Options panel). Re-creates the window and fonts at
 * the new size; the screen contents are kept. Also used to add or remove
 * the status row (gfx_status_enable() first, then gfx_set_zoom(cfg_zoom)). */
void gfx_set_zoom(int zoom) {
  int old_zoom = cfg_zoom;
  SDL_Surface *newscreen;

  if (zoom < 1) return;
  zoom = gfx_clamp_zoom(zoom);   /* keep the window on-screen and column-valid */

  cfg_zoom = zoom;
  charwidth = (cfg_columns == 40 ? 8 : 4) * cfg_zoom;
  charheight = 8 * cfg_zoom;
  gfx_width = cfg_zoom * GFX_WIDTH;
  gfx_height = cfg_zoom * GFX_HEIGHT;

  newscreen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp,
      (SDL_FULLSCREEN * cfg_fullscreen)|(cfg_fullscreen ? 0 : SDL_RESIZABLE)|SDL_ANYFORMAT|SDL_SWSURFACE);
  if (newscreen == NULL) {
    printf("Unable to resize window: %s\n", SDL_GetError());
    cfg_zoom = old_zoom;
    charwidth = (cfg_columns == 40 ? 8 : 4) * cfg_zoom;
    charheight = 8 * cfg_zoom;
    gfx_width = cfg_zoom * GFX_WIDTH;
    gfx_height = cfg_zoom * GFX_HEIGHT;
    return;
  }
  gfx_screen = newscreen;

  gfx_destroyfont(fontlist[0]);
  gfx_destroyfont(fontlist[1]);
  gfx_destroyfont(fontlist[2]);
  fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom);
  fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom);
  fontlist[2] = gfx_createfont_ansi(rawfont[2], cfg_zoom, vga_palette);

  if (menu_init(gfx_width, gfx_height)) {
    printf("menu_init failed on zoom change: %s\n", SDL_GetError());
    return;
  }
  gfx_menu_width = gfx_width;
  gfx_menu_height = gfx_height;
  gfx_menu_lastline = (gfx_height - 1) / charheight;

  SDL_FillRect(gfx_screen, NULL, SDL_MapRGB(gfx_screen->format, 0, 0, 0));
  memset(dirty, SDL_TRUE, sizeof(dirty));
  status_dirty = 1;
}


void gfx_copy_rect(int rect_x, int rect_y, int rect_w, int rect_h, unsigned char *rect_0400, unsigned char *rect_d800) {
  int x, y;

  resetcursor();
  for (y = 0; y < rect_h; ++y) {
    dirty[rect_y + y] = SDL_TRUE;
    for (x = 0; x < rect_w; ++x) {
      *rect_0400++ = gfx_0400[(rect_y + y) * cfg_columns + rect_x + x];
      *rect_d800++ = gfx_d800[(rect_y + y) * cfg_columns + rect_x + x];
    }
  }
}


void gfx_clear_rect(int rect_x, int rect_y, int rect_w, int rect_h) {
  int x, y;

  resetcursor();
  for (y = 0; y < rect_h; ++y) {
    dirty[rect_y + y] = SDL_TRUE;
    for (x = 0; x < rect_w; ++x) {
      gfx_0400[(rect_y + y) * cfg_columns + rect_x + x] = 32;
      gfx_d800[(rect_y + y) * cfg_columns + rect_x + x] = fgcolor;
    }
  }
}


void gfx_paste_rect(int rect_x, int rect_y, int rect_w, int rect_h, unsigned char *rect_0400, unsigned char *rect_d800) {
  int x, y;

  resetcursor();
  /* The paste origin is the (movable) cursor, so rect_x/rect_y + the saved
   * rect_w/rect_h can run past the screen. Always advance the source pointer
   * by the full rectangle (to keep rows aligned) but only write cells that
   * fall inside the 25-row x cfg_columns screen buffer (and dirty[25]). */
  for (y = 0; y < rect_h; ++y) {
    int dy = rect_y + y;
    if (dy >= 0 && dy < 25) dirty[dy] = SDL_TRUE;
    for (x = 0; x < rect_w; ++x) {
      int dx = rect_x + x;
      unsigned char ch = *rect_0400++;
      unsigned char co = *rect_d800++;
      if (dy >= 0 && dy < 25 && dx >= 0 && dx < cfg_columns) {
        gfx_0400[dy * cfg_columns + dx] = ch;
        gfx_d800[dy * cfg_columns + dx] = co;
      }
    }
  }
}


int gfx_save_screenshot(const char *filename) {
  if (SDL_SaveBMP(gfx_screen, filename) == 0) {
    return 0;
  }
  return -1;
}


/* ------------------------------------------------------------------
 * Animated startup art (assets/startup.ans).
 *
 * An 80-column ANSI picture is rendered once into a 640x400 RGB image
 * (8x16 cells). While it is on screen gfx_vbl() redraws it about 25
 * times a second with two effects on top: a pulsing glow (a blurred
 * copy of the picture added back) and a band of light that sweeps
 * across the lit parts every few seconds.
 * The animation stops the moment anything else draws: terminal output,
 * a menu, a zoom or mode change. What is on screen then simply stays.
 * ------------------------------------------------------------------ */
#define ART_W 640
#define ART_H 400
#define ART_COLS 80
#define ART_ROWS 25
#define ART_FRAME_MS 40

static unsigned char *art_base;      /* ART_W*ART_H*3, the picture */
static unsigned char *art_glow;      /* ART_W*ART_H*3, blurred picture */
static SDL_Surface *art_frame;       /* 32 bpp, screen channel order */
static int art_active;
static Uint32 art_epoch, art_last;

static void art_free(void) {
  free(art_base); art_base = NULL;
  free(art_glow); art_glow = NULL;
}

/* One horizontal + one vertical box blur pass, radius r, in place. */
static void art_blur(unsigned char *img, unsigned char *tmp, int r) {
  int x, y, ch, k;
  for (y = 0; y < ART_H; y++) {
    for (ch = 0; ch < 3; ch++) {
      int sum = 0;
      for (k = -r; k <= r; k++) {
        int xx = k < 0 ? 0 : k;
        sum += img[(y * ART_W + xx) * 3 + ch];
      }
      for (x = 0; x < ART_W; x++) {
        int add = x + r + 1, sub = x - r;
        tmp[(y * ART_W + x) * 3 + ch] = (unsigned char)(sum / (2 * r + 1));
        if (add >= ART_W) add = ART_W - 1;
        if (sub < 0) sub = 0;
        sum += img[(y * ART_W + add) * 3 + ch] - img[(y * ART_W + sub) * 3 + ch];
      }
    }
  }
  for (x = 0; x < ART_W; x++) {
    for (ch = 0; ch < 3; ch++) {
      int sum = 0;
      for (k = -r; k <= r; k++) {
        int yy = k < 0 ? 0 : k;
        sum += tmp[(yy * ART_W + x) * 3 + ch];
      }
      for (y = 0; y < ART_H; y++) {
        int add = y + r + 1, sub = y - r;
        img[(y * ART_W + x) * 3 + ch] = (unsigned char)(sum / (2 * r + 1));
        if (add >= ART_H) add = ART_H - 1;
        if (sub < 0) sub = 0;
        sum += tmp[(add * ART_W + x) * 3 + ch] - tmp[(sub * ART_W + x) * 3 + ch];
      }
    }
  }
}

/* Parse the ANSI file and render it. Understands what ANSI art uses:
 * SGR colours (0, 1, 30-37, 40-47), cursor right/left/up/down, cursor
 * position, CR, LF, and stops at the SAUCE marker (0x1A). */
static int art_load(const char *path) {
  static const unsigned char pal[16][3] = {
    {0x00,0x00,0x00},{0xaa,0x00,0x00},{0x00,0xaa,0x00},{0xaa,0x55,0x00},
    {0x00,0x00,0xaa},{0xaa,0x00,0xaa},{0x00,0xaa,0xaa},{0xaa,0xaa,0xaa},
    {0x55,0x55,0x55},{0xff,0x55,0x55},{0x55,0xff,0x55},{0xff,0xff,0x55},
    {0x55,0x55,0xff},{0xff,0x55,0xff},{0x55,0xff,0xff},{0xff,0xff,0xff}
  };
  static unsigned char cell_ch[ART_ROWS][ART_COLS];
  static unsigned char cell_fg[ART_ROWS][ART_COLS];
  static unsigned char cell_bg[ART_ROWS][ART_COLS];
  unsigned char *tmp;
  FILE *f;
  int c, cx = 0, cy = 0, fg = 7, bg = 0, bold = 0;
  int state = 0, np = 0, par[8];
  int row, col, used_rows = 0, yoff, i;
  long nread = 0;

  f = fopen(path, "rb");
  if (!f) return 0;
  memset(cell_ch, ' ', sizeof(cell_ch));
  memset(cell_fg, 7, sizeof(cell_fg));
  memset(cell_bg, 0, sizeof(cell_bg));
  memset(par, 0, sizeof(par));

  while ((c = fgetc(f)) != EOF && c != 0x1a && ++nread < 200000) {
    if (state == 0) {
      if (c == 0x1b) { state = 1; }
      else if (c == '\r') { cx = 0; }
      else if (c == '\n') { if (cy < ART_ROWS) cy++; }
      else {
        if (cx >= ART_COLS) { cx = 0; if (cy < ART_ROWS) cy++; }
        if (cy < ART_ROWS) {
          cell_ch[cy][cx] = (unsigned char)c;
          cell_fg[cy][cx] = (unsigned char)(fg + (bold ? 8 : 0));
          cell_bg[cy][cx] = (unsigned char)bg;
          if (cy + 1 > used_rows && c != ' ') used_rows = cy + 1;
        }
        cx++;
      }
    } else if (state == 1) {
      if (c == '[') { state = 2; np = 0; memset(par, 0, sizeof(par)); }
      else state = 0;
    } else {
      if (c >= '0' && c <= '9') {
        if (np < 8 && par[np] < 10000) par[np] = par[np] * 10 + (c - '0');
      } else if (c == ';') {
        if (np < 8) np++;
      } else if (c == '?') {
        /* private marker, ignored */
      } else {
        int n = par[0] ? par[0] : 1;
        if (c == 'm') {
          for (i = 0; i <= np && i < 8; i++) {
            int v = par[i];
            if (v == 0) { fg = 7; bg = 0; bold = 0; }
            else if (v == 1) bold = 1;
            else if (v == 22) bold = 0;
            else if (v >= 30 && v <= 37) fg = v - 30;
            else if (v >= 40 && v <= 47) bg = v - 40;
          }
        } else if (c == 'C') { cx += n; }
        else if (c == 'D') { cx -= n; }
        else if (c == 'A') { cy -= n; }
        else if (c == 'B') { cy += n; }
        else if (c == 'H' || c == 'f') {
          cy = par[0] ? par[0] - 1 : 0;
          cx = (np >= 1 && par[1]) ? par[1] - 1 : 0;
        }
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > ART_COLS) cx = ART_COLS;
        if (cy > ART_ROWS) cy = ART_ROWS;
        state = 0;
      }
    }
  }
  fclose(f);
  if (used_rows == 0) return 0;

  art_free();
  art_base = calloc(ART_W * ART_H, 3);
  art_glow = malloc(ART_W * ART_H * 3);
  tmp = malloc(ART_W * ART_H * 3);
  if (!art_base || !art_glow || !tmp) {
    free(tmp);
    art_free();
    return 0;
  }

  /* centre the used rows vertically, above the bottom text row that
   * carries the "press ESC" hint (a full 25-row picture shares it) */
  yoff = (ART_H - 16 - used_rows * 16) / 2;
  if (yoff < 0) yoff = 0;
  for (row = 0; row < used_rows && row < ART_ROWS; row++) {
    for (col = 0; col < ART_COLS; col++) {
      const unsigned char *g = cp437_glyph(cell_ch[row][col]);
      const unsigned char *f3 = pal[cell_fg[row][col] & 15];
      const unsigned char *b3 = pal[cell_bg[row][col] & 7];
      int gy, gx;
      for (gy = 0; gy < 16; gy++) {
        int y = yoff + row * 16 + gy;
        unsigned char bits = g[gy / 2];
        if (y >= ART_H) break;
        for (gx = 0; gx < 8; gx++) {
          const unsigned char *src = (bits & (0x80 >> gx)) ? f3 : b3;
          unsigned char *d = art_base + (y * ART_W + col * 8 + gx) * 3;
          d[0] = src[0]; d[1] = src[1]; d[2] = src[2];
        }
      }
    }
  }

  /* glow source: the picture itself, blurred twice (close to a gaussian) */
  memcpy(art_glow, art_base, ART_W * ART_H * 3);
  art_blur(art_glow, tmp, 5);
  art_blur(art_glow, tmp, 4);
  free(tmp);
  return 1;
}

/* Compose one frame at time t (ms since the picture appeared) and put it
 * on the screen. */
static void art_draw(Uint32 t) {
  const SDL_PixelFormat *fm = art_frame->format;
  int rs = fm->Rshift, gs = fm->Gshift, bs = fm->Bshift;
  Uint32 *px;
  int stride, x, y, i;
  /* glow strength on a 0..256 scale: a slow pulse */
  int glow = (int)(170.0f + 80.0f * sinf((float)t * 0.0021f));
  /* shine: one pass of 1.5 s every 6 s, moving along x + y/2 */
  Uint32 cyc = t % 6000;
  int shine_on = cyc < 1500;
  int shine_pos = shine_on ? (int)((float)cyc / 1500.0f * (ART_W + ART_H / 2 + 240)) - 120 : 0;
  SDL_Rect dst;

  if (SDL_MUSTLOCK(art_frame)) SDL_LockSurface(art_frame);
  px = (Uint32 *)art_frame->pixels;
  stride = art_frame->pitch / 4;
  for (y = 0; y < ART_H; y++) {
    const unsigned char *b = art_base + y * ART_W * 3;
    const unsigned char *g = art_glow + y * ART_W * 3;
    Uint32 *row = px + y * stride;
    for (x = 0; x < ART_W; x++, b += 3, g += 3) {
      /* the halo belongs on the dark around the shapes; on a pixel that
       * is already lit only a little is added, or colours wash out */
      int mx = b[0] > b[1] ? b[0] : b[1];
      int k, r, gg, bb;
      if (b[2] > mx) mx = b[2];
      k = (glow * (56 + ((255 - mx) * 200) / 255)) >> 8;
      r = b[0] + ((g[0] * k) >> 8);
      gg = b[1] + ((g[1] * k) >> 8);
      bb = b[2] + ((g[2] * k) >> 8);
      if (shine_on && (b[0] | b[1] | b[2])) {
        int d = x + y / 2 - shine_pos;
        if (d < 0) d = -d;
        if (d < 60) {
          int w = 60 - d;                       /* 0..60 */
          int lum = (b[0] + b[1] + b[2]) / 3;   /* only lit pixels shine */
          int add = (w * w * (60 + lum)) / 3600;
          r += add; gg += add; bb += add;
        }
      }
      if (r > 255) r = 255;
      if (gg > 255) gg = 255;
      if (bb > 255) bb = 255;
      row[x] = ((Uint32)r << rs) | ((Uint32)gg << gs) | ((Uint32)bb << bs);
    }
  }


  /* the hint on the bottom row, gently breathing */
  {
    static const char hint[] = "\x10 PRESS ESC FOR MENU \x11";
    int len = (int)sizeof(hint) - 1;
    int x0 = (ART_W - len * 8) / 2, y0 = ART_H - 16;
    int lvl = (int)(190.0f + 65.0f * sinf((float)t * 0.004f));   /* 125..255 */
    Uint32 col = ((Uint32)(0x55 * lvl / 255) << rs) | ((Uint32)lvl << gs) | ((Uint32)lvl << bs);
    for (i = 0; i < len; i++) {
      const unsigned char *g = cp437_glyph((unsigned char)hint[i]);
      for (y = 0; y < 16; y++) {
        unsigned char bits = g[y / 2];
        for (x = 0; x < 8; x++) {
          if (bits & (0x80 >> x)) px[(y0 + y) * stride + x0 + i * 8 + x] = col;
        }
      }
    }
  }
  if (SDL_MUSTLOCK(art_frame)) SDL_UnlockSurface(art_frame);

  dst.x = 0; dst.y = 0; dst.w = gfx_width; dst.h = gfx_height;
  if (gfx_width == ART_W && gfx_height == ART_H) {
    SDL_BlitSurface(art_frame, NULL, gfx_screen, &dst);
  } else if (SDL_SoftStretch(art_frame, NULL, gfx_screen, &dst) != 0) {
    art_active = 0;
    return;
  }
  SDL_UpdateRect(gfx_screen, 0, 0, gfx_width, gfx_height);
}

/* Show assets/startup.ans with its animation. Returns 1 when it is on
 * screen, 0 when the file is missing or the display cannot do it (the
 * caller then falls back to the still picture). */
int gfx_show_startup_art(void) {
  char path[1024];
  const SDL_PixelFormat *sf = gfx_screen->format;

  art_active = 0;
  /* The frame shares the screen's pixel layout so it can be stretched
   * straight onto it; that needs a true-colour screen. */
  if (sf->BitsPerPixel != 32 || sf->Rloss || sf->Gloss || sf->Bloss) return 0;
  if (!art_base) {
    path_build_asset(path, sizeof(path), "startup.ans");
    if (!art_load(path)) return 0;
  }
  if (art_frame) { SDL_FreeSurface(art_frame); art_frame = NULL; }
  art_frame = SDL_CreateRGBSurface(SDL_SWSURFACE, ART_W, ART_H, 32,
                                   sf->Rmask, sf->Gmask, sf->Bmask, 0);
  if (!art_frame) return 0;

  gfx_cursor_show(0);
  gfx_vbl();                                /* flush pending text first */
  memset(dirty, SDL_FALSE, sizeof(dirty));
  art_epoch = SDL_GetTicks();
  art_last = art_epoch;
  art_active = 1;
  art_draw(0);
  return art_active;
}

/* Called from gfx_vbl(). had_output: something else was drawn this frame. */
static void art_tick(int had_output) {
  Uint32 now;
  if (!art_active) return;
  if (had_output || menu_visible) { art_active = 0; return; }
  now = SDL_GetTicks();
  if (now - art_last < ART_FRAME_MS) return;
  art_last = now;
  art_draw(now - art_epoch);
}

void gfx_show_startup_bg(void) {
  char path[1024];
  FILE *f;

  /* the animated ANSI logo, when it is there, in either mode */
  if (gfx_show_startup_art()) return;

  if (cfg_termmode == 1) {
    /* ANSI mode: try background.ans first */
    path_build_asset(path, sizeof(path), "background.ans");
    f = fopen(path, "rb");
    if (f) {
      int c;
      extern void ansi_out(unsigned char byte);
      while ((c = fgetc(f)) != EOF)
        ansi_out((unsigned char)c);
      fclose(f);
      gfx_vbl();
      return;
    }
    /* Fall back to background_80.bmp */
    path_build_asset(path, sizeof(path), "background_80.bmp");
    if (cfg_file_exists(path)) {
      gfx_cursor_show(0);
      gfx_vbl();
      gfx_show_background(path);
      return;
    }
    gfx_vbl();
  } else {
    /* PETSCII mode: try background.seq first */
    path_build_asset(path, sizeof(path), "background.seq");
    f = fopen(path, "rb");
    if (f) {
      int c;
      ffd2(147);  /* clear screen */
      while ((c = fgetc(f)) != EOF)
        ffd2((unsigned char)c);
      fclose(f);
      gfx_vbl();
      return;
    }
    /* Fall back to background_40.bmp */
    path_build_asset(path, sizeof(path), "background_40.bmp");
    if (cfg_file_exists(path)) {
      gfx_cursor_show(0);
      gfx_vbl();
      gfx_show_background(path);
      return;
    }
    /* Built-in PETSCII banner */
    if (cfg_columns == 40) {
      extern void print(const char *s);
      print("\x12\x1f            \x9a\xac\x9f\xa2\xa2\x99\xa2\xa2\x9e\xa2\xa2\x05\xa2\xa2\x9e\xa2\xa2\x99\xa2\xa2\x9f\xa2\xa2\x9a\xbb\x1f            ");
      print("\x12\x1f            \x92                \x12            ");
      print("\x12\x1f            \x92\x9e  cg\x96tERM \x05" "3.0    \x12\x1f            ");
      print("\x12\x1f            \x92                \x12            ");
      print("\x12\x1f            \x9a\xbc\x92\x9f\xa2\xa2\x99\xa2\xa2\x9e\xa2\xa2\x05\xa2\xa2\x9e\xa2\xa2\x99\xa2\xa2\x9f\xa2\xa2\x12\x9a\xbe\x1f            ");
      print("\x92\x05\x0d");
    }
    gfx_vbl();
  }
}


void gfx_show_background(const char *bmpfile) {
  SDL_Surface *raw = SDL_LoadBMP(bmpfile);
  SDL_Surface *bg;
  if (!raw) return;
  /* Match the screen's pixel format so SDL_SoftStretch can scale it. */
  bg = SDL_DisplayFormat(raw);
  SDL_FreeSurface(raw);
  if (!bg) return;

  if (bg->w == gfx_width && bg->h == gfx_height) {
    SDL_BlitSurface(bg, NULL, gfx_screen, NULL);
  } else {
    /* Scale the art to fill the screen. The backgrounds and the C64 screen
     * share the same 1.6 aspect, so this is a clean uniform scale with no
     * black bars at any zoom (the old code centred it and left big margins). */
    SDL_Rect dst;
    dst.x = 0; dst.y = 0; dst.w = gfx_width; dst.h = gfx_height;
    if (SDL_SoftStretch(bg, NULL, gfx_screen, &dst) != 0) {
      /* Stretch unsupported for this format: centre at native size. */
      SDL_Rect c;
      c.x = (gfx_width - bg->w) / 2; if (c.x < 0) c.x = 0;
      c.y = (gfx_height - bg->h) / 2; if (c.y < 0) c.y = 0;
      SDL_FillRect(gfx_screen, NULL, SDL_MapRGB(gfx_screen->format, 0, 0, 0));
      SDL_BlitSurface(bg, NULL, gfx_screen, &c);
    }
  }
  SDL_FreeSurface(bg);
  SDL_UpdateRect(gfx_screen, 0, 0, 0, 0);
  /* Mark all lines clean so gfx_vbl doesn't overwrite */
  memset(dirty, SDL_FALSE, sizeof(dirty));
}


void gfx_crt_shutdown(void) {
  /* Classic CRT power-off effect:
   * 1. Screen squishes vertically to a horizontal line
   * 2. Line shrinks to a dot
   * 3. Dot glows briefly then fades */
  int w = gfx_width;
  int h = gfx_height;
  int cx = w / 2;
  int cy = h / 2;
  SDL_Surface *snapshot;
  Uint32 black;
  int phase;

  /* Capture current screen */
  snapshot = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h,
    gfx_screen->format->BitsPerPixel,
    gfx_screen->format->Rmask, gfx_screen->format->Gmask,
    gfx_screen->format->Bmask, gfx_screen->format->Amask);
  if (!snapshot) return;
  SDL_BlitSurface(gfx_screen, NULL, snapshot, NULL);
  black = SDL_MapRGB(gfx_screen->format, 0, 0, 0);

  /* Phase 1: Vertical squeeze — screen collapses to a horizontal line (~0.4 sec) */
  for (phase = 0; phase < 20; phase++) {
    float progress = (float)phase / 20.0f;
    /* Ease-in: accelerate */
    float ease = progress * progress;
    int squeeze_h = (int)(h * (1.0f - ease));
    int top = cy - squeeze_h / 2;
    if (squeeze_h < 2) squeeze_h = 2;
    if (top < 0) top = 0;

    SDL_FillRect(gfx_screen, NULL, black);
    /* SDL 1.2 doesn't scale blits, so we sample rows */
    {
      int dy;
      SDL_LockSurface(snapshot);
      SDL_LockSurface(gfx_screen);
      for (dy = 0; dy < squeeze_h; dy++) {
        int src_y = dy * h / squeeze_h;
        if (src_y >= h) src_y = h - 1;
        memcpy(
          (Uint8 *)gfx_screen->pixels + (top + dy) * gfx_screen->pitch,
          (Uint8 *)snapshot->pixels + src_y * snapshot->pitch,
          w * gfx_screen->format->BytesPerPixel);
      }
      SDL_UnlockSurface(gfx_screen);
      SDL_UnlockSurface(snapshot);
    }

    /* Add slight brightness boost to the squeeze line */
    if (squeeze_h < 20) {
      SDL_Rect glow;
      Uint32 white = SDL_MapRGB(gfx_screen->format, 200, 200, 200);
      glow.x = 0; glow.y = cy - 1; glow.w = w; glow.h = 2;
      SDL_FillRect(gfx_screen, &glow, white);
    }

    SDL_UpdateRect(gfx_screen, 0, 0, w, h);
    SDL_Delay(20);
  }

  /* Phase 2: Horizontal line — bright white, full width (~0.15 sec) */
  {
    int lf;
    for (lf = 0; lf < 8; lf++) {
      Uint32 white = SDL_MapRGB(gfx_screen->format, 255, 255, 255);
      SDL_Rect line_r;
      SDL_FillRect(gfx_screen, NULL, black);
      line_r.x = 0; line_r.y = cy - 1; line_r.w = w; line_r.h = 3;
      SDL_FillRect(gfx_screen, &line_r, white);
      SDL_UpdateRect(gfx_screen, 0, 0, w, h);
      SDL_Delay(20);
    }
  }

  /* Phase 3: Line shrinks to a dot (~0.3 sec) */
  {
    int sf;
    for (sf = 0; sf < 15; sf++) {
      float progress = (float)sf / 15.0f;
      float ease = progress * progress;
      int line_w = (int)(w * (1.0f - ease));
      int bright = 255 - (int)(100 * progress);
      Uint32 col;
      SDL_Rect dot_r;

      if (line_w < 4) line_w = 4;
      if (bright < 100) bright = 100;
      col = SDL_MapRGB(gfx_screen->format, bright, bright, bright);

      SDL_FillRect(gfx_screen, NULL, black);
      dot_r.x = cx - line_w / 2; dot_r.y = cy - 1;
      dot_r.w = line_w; dot_r.h = 3;
      SDL_FillRect(gfx_screen, &dot_r, col);
      SDL_UpdateRect(gfx_screen, 0, 0, w, h);
      SDL_Delay(20);
    }
  }

  /* Phase 4: Glowing dot fades out (~0.5 sec) */
  {
    int df;
    for (df = 0; df < 25; df++) {
      float progress = (float)df / 25.0f;
      int bright = (int)(180 * (1.0f - progress) * (1.0f - progress));
      int dot_size = 6 - (int)(4 * progress);
      Uint32 col;
      SDL_Rect dot_r;

      if (dot_size < 2) dot_size = 2;
      if (bright < 0) bright = 0;
      col = SDL_MapRGB(gfx_screen->format, bright, bright, bright + bright / 4);

      SDL_FillRect(gfx_screen, NULL, black);
      dot_r.x = cx - dot_size / 2; dot_r.y = cy - dot_size / 2;
      dot_r.w = dot_size; dot_r.h = dot_size;
      SDL_FillRect(gfx_screen, &dot_r, col);
      SDL_UpdateRect(gfx_screen, 0, 0, w, h);
      SDL_Delay(20);
    }
  }

  /* Final black */
  SDL_FillRect(gfx_screen, NULL, black);
  SDL_UpdateRect(gfx_screen, 0, 0, w, h);
  SDL_Delay(300);

  SDL_FreeSurface(snapshot);
}


/* ---------------------------------------------------------------------------
 * ANSI terminal helpers (appended for ansi.c).
 *
 * All of these work on the live screen (gfx_0400 / gfx_d800 / gfx_bg), never
 * on the scrollback part of the buffers, and every vacated cell is filled with
 * a space in the caller's fg/bg colours so the terminal can implement BCE
 * (background colour erase) the way xterm and SyncTERM do.
 * ------------------------------------------------------------------------- */

/* Fill `count` cells of row y starting at column x with a blank in fg/bg.
 * Clipped to the row; a count that runs past the right edge stops there. */
void gfx_fill_cells(int x, int y, int count, int fg, int bg) {
  int offset;

  if (y < 0 || y > 24 || x < 0 || x >= cfg_columns || count <= 0) {
    return;
  }
  if (x + count > cfg_columns) {
    count = cfg_columns - x;
  }
  resetcursor();
  offset = y * cfg_columns + x;
  memset(gfx_0400 + offset, ' ', count);
  memset(gfx_d800 + offset, fg, count);
  memset(gfx_bg + offset, bg, count);
  dirty[y] = SDL_TRUE;
}


/* Scroll rows top..bottom (inclusive) by n: n > 0 moves content up (rows
 * vacated at the bottom), n < 0 moves content down (rows vacated at the top).
 * Rows outside the range are untouched; this is the DECSTBM scroll-region
 * primitive used by LF/IND/RI/IL/DL/SU/SD. Nothing goes to scrollback. */
void gfx_scroll_region(int top, int bottom, int n, int fg, int bg) {
  int rows, abs_n, y;

  if (top < 0) top = 0;
  if (bottom > 24) bottom = 24;
  if (top > bottom || n == 0) {
    return;
  }
  rows = bottom - top + 1;
  abs_n = n < 0 ? -n : n;
  if (abs_n > rows) {
    abs_n = rows;
  }
  resetcursor();
  if (abs_n < rows) {
    int count = (rows - abs_n) * cfg_columns;
    int src, dst;
    if (n > 0) {
      src = (top + abs_n) * cfg_columns;
      dst = top * cfg_columns;
    } else {
      src = top * cfg_columns;
      dst = (top + abs_n) * cfg_columns;
    }
    memmove(gfx_0400 + dst, gfx_0400 + src, count);
    memmove(gfx_d800 + dst, gfx_d800 + src, count);
    memmove(gfx_bg + dst, gfx_bg + src, count);
  }
  if (n > 0) {
    for (y = bottom - abs_n + 1; y <= bottom; y++) {
      gfx_fill_cells(0, y, cfg_columns, fg, bg);
    }
  } else {
    for (y = top; y < top + abs_n; y++) {
      gfx_fill_cells(0, y, cfg_columns, fg, bg);
    }
  }
  for (y = top; y <= bottom; y++) {
    dirty[y] = SDL_TRUE;
  }
}


/* ICH: open n blank cells at (x, y), shifting the rest of the row right.
 * Cells pushed past the right margin are lost. */
void gfx_insert_cells(int x, int y, int n, int fg, int bg) {
  int offset, moving;

  if (y < 0 || y > 24 || x < 0 || x >= cfg_columns || n <= 0) {
    return;
  }
  if (n > cfg_columns - x) {
    n = cfg_columns - x;
  }
  resetcursor();
  offset = y * cfg_columns + x;
  moving = cfg_columns - x - n;
  if (moving > 0) {
    memmove(gfx_0400 + offset + n, gfx_0400 + offset, moving);
    memmove(gfx_d800 + offset + n, gfx_d800 + offset, moving);
    memmove(gfx_bg + offset + n, gfx_bg + offset, moving);
  }
  gfx_fill_cells(x, y, n, fg, bg);
}


/* DCH: remove n cells at (x, y), shifting the rest of the row left and
 * filling the vacated cells at the right margin with blanks in fg/bg. */
void gfx_delete_cells(int x, int y, int n, int fg, int bg) {
  int offset, moving;

  if (y < 0 || y > 24 || x < 0 || x >= cfg_columns || n <= 0) {
    return;
  }
  if (n > cfg_columns - x) {
    n = cfg_columns - x;
  }
  resetcursor();
  offset = y * cfg_columns + x;
  moving = cfg_columns - x - n;
  if (moving > 0) {
    memmove(gfx_0400 + offset, gfx_0400 + offset + n, moving);
    memmove(gfx_d800 + offset, gfx_d800 + offset + n, moving);
    memmove(gfx_bg + offset, gfx_bg + offset + n, moving);
  }
  gfx_fill_cells(cfg_columns - n, y, n, fg, bg);
}


/* Window dragged to a new size (SDL_VIDEORESIZE). The C64 screen is drawn
 * with integer-scaled 8x8 cells, so the window snaps to the nearest whole
 * zoom factor (1x-8x; 80 columns needs an even one) and the new zoom is
 * kept in the config. Dragging a little smaller or larger than the current
 * size therefore steps the zoom like the Options panel does. */
void gfx_handle_resize(int w, int h) {
  int z;

  if (cfg_fullscreen) return;
  /* Smooth-scale mode: let the SDL backend stretch the fixed-resolution
   * surface to whatever size the window is, so it fills any size the user
   * drags to (issue #1). We just ignore the resize instead of snapping. */
  if (cfg_smoothscale) return;
  if (w == gfx_width && h == gfx_height + gfx_status_rows * charheight) return;   /* our own re-mode */
  z = (w + GFX_WIDTH / 2) / GFX_WIDTH;
  {
    int zh = (h + GFX_HEIGHT / 2) / (GFX_HEIGHT + gfx_status_rows * 8);
    if (zh < z) z = zh;          /* fit both dimensions */
  }
  if (z < 1) z = 1;
  if (z > 8) z = 8;
  if (z > gfx_max_zoom()) z = gfx_max_zoom();   /* never grow past the display (issue #12) */
  if (cfg_columns == 80 && (z & 1)) z = (z > 1) ? z - 1 : 2;
  /* Only re-create the window when the zoom step actually changes. A drag
   * fires a stream of VIDEORESIZE events; re-moding on every one made the
   * window flicker and thrash. While dragging within one zoom step the
   * SDL2 backend scales our surface to the window, so nothing is lost. */
  if (z != cfg_zoom) {
    gfx_set_zoom(z);             /* snaps to the exact integer-zoom size */
    if (z == cfg_zoom) {
      char val[8];
      snprintf(val, sizeof(val), "%d", cfg_zoom);
      cfg_save_setting("zoom", val);
    }
  }
}


/* Re-present the entire window after the OS discarded its contents — e.g. the
 * window was raised from behind another, un-minimized, or regained focus after
 * an alt-tab. The pixels are intact in the software surface, so a full-surface
 * UpdateRect simply pushes them back to the display (GitHub issue #11). */
void gfx_force_redraw(void) {
  if (gfx_screen) {
    SDL_UpdateRect(gfx_screen, 0, 0, 0, 0);   /* 0,0,0,0 == whole surface */
  }
}
