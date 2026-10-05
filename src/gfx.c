#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static int cursorspeed = 22;
static int gfx_height, gfx_width;
static Uint8 charwidth, charheight;
static int borderleft, borderright;
static Uint8 cursorctr, colorundercursor;
static SDL_bool cursorvis;
static int cursor_hidden = 0;   /* gfx_cursor_show(0): no blink at all */

/* Optional status row drawn below the 25 terminal rows (cgterm only; cgchat
 * and cgedit keep the plain window). The text is kept as ASCII and converted
 * to the active font's codes at draw time. */
static int gfx_status_rows = 0;
static char status_text[81];
static unsigned char status_color = 1;
static unsigned char status_bg = 0;
static int status_dirty = 0;
static void gfx_draw_status_row(void);
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
 * Each source row maps to one output row (no vertical zoom needed). */
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
      /* Read 16 source rows, map to charheight output rows */
      for (y = 0; y < src_char_h && y < charheight; ++y) {
	  sp = (Uint8 *)srcsurface->pixels + (c & 0x1f) * 8 + ((c / 32) * src_char_h + y) * srcsurface->pitch;
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
  gfx_0400 = gfx_0400_buffer + gfx_maxoffset;
  gfx_d800 = gfx_d800_buffer + gfx_maxoffset;
  gfx_bg = gfx_bg_buffer + gfx_maxoffset;

  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0) {
    printf("Unable to init SDL: %s\n", SDL_GetError());
    return(1);
  }
  atexit(SDL_Quit);
  vidinfo = SDL_GetVideoInfo();
  if ((gfx_bpp = vidinfo->vfmt->BitsPerPixel) < 15) {
    gfx_bpp = 16;
  }
  if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp, (SDL_FULLSCREEN * fullscreen)|SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
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

  cursorctr = 0;
  cursorvis = SDL_FALSE;

  gfx_cursdirection = 1;

  return(0);
}


void invertcursor(void) {
  gfx_0400[gfx_cursy * cfg_columns + gfx_cursx] ^= 0x80;
  dirty[gfx_cursy] = SDL_TRUE;
}


void resetcursor(void) {
  if (cursorvis) {
    invertcursor();
    cursorvis = SDL_FALSE;
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = colorundercursor;
  }
  cursorctr = cursorspeed/2 - 1;
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
  unsigned char converted[80 * 3 + 1];
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
  if (menu_visible && ypos >= gfx_menu_firstline && ypos <= gfx_menu_lastline) {
    src.w = gfx_menu_width;
    src.h = charheight;
    src.x = 0;
    src.y = ypos * charheight - gfx_menu_ypos;
    dest.x = gfx_menu_xpos;
    dest.y = ypos * charheight;
    SDL_BlitSurface(menu_surface, &src, gfx_screen, &dest);
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
  sc &= 0x7F;
  if (fnt == 2) return (sc >= 0x20 && sc < 0x7F) ? (char)sc : '?';
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
  SDL_UpdateRect(gfx_screen, 0, 25 * charheight, gfx_width - 1, charheight);
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

  cursorctr = (cursorctr + 1) % cursorspeed;
  if (cursor_hidden) {
    /* ESC[?25l, splash, background image: no blink at all */
  } else if (cursorctr == cursorspeed/2) {
    invertcursor();
    cursorvis = SDL_TRUE;
    colorundercursor = gfx_d800[gfx_cursy * cfg_columns + gfx_cursx];
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = fgcolor;
  } else if (cursorctr == 0 && cursorvis) {
    invertcursor();
    cursorvis = SDL_FALSE;
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = colorundercursor;
  }

  if (menu_dirty) {
    menu_dirty = SDL_FALSE;
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
    SDL_UpdateRect(gfx_screen, 0, first * charheight, gfx_width - 1, (last - first) * charheight + charheight);
  }
  if (gfx_status_rows && (status_dirty || first != 25)) {
    gfx_draw_status_row();
    status_dirty = 0;
  }

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
    if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp, SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
      printf("Unable to open window: %s\n", SDL_GetError());
      exit(1);
    }
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(SDL_DISABLE);  /* stay hidden in windowed mode too */
    cfg_fullscreen = 0;
  } else {
    if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp, SDL_FULLSCREEN|SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
      printf("Unable to open window: %s\n", SDL_GetError());
      exit(1);
    }
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
  int old_charwidth = charwidth, old_width = gfx_width, old_height = gfx_height;

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
    charwidth = 8 * cfg_zoom;
  } else {
    /* Save zoom before forcing it for 80 cols */
    if (cfg_zoom == 1 || cfg_zoom == 3) {
      saved_zoom = cfg_zoom;
      cfg_zoom = 2;
    }
    charwidth = 4 * cfg_zoom;
  }
  gfx_width = cfg_zoom * GFX_WIDTH;
  gfx_height = cfg_zoom * GFX_HEIGHT;

  {
    SDL_Surface *newscreen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp,
        (SDL_FULLSCREEN * cfg_fullscreen)|SDL_ANYFORMAT|SDL_SWSURFACE);
    if (newscreen == NULL) {
      /* keep the existing surface and layout rather than leaving gfx_screen NULL */
      printf("Unable to resize window: %s\n", SDL_GetError());
      cfg_columns = old_columns;
      cfg_zoom = old_zoom;
      saved_zoom = old_saved_zoom;
      charwidth = old_charwidth;
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

  if (zoom < 1 || zoom > 8) return;
  if (cfg_columns == 80 && (zoom & 1)) return;   /* 80 columns needs 4*zoom pixel cells */

  cfg_zoom = zoom;
  charwidth = (cfg_columns == 40 ? 8 : 4) * cfg_zoom;
  charheight = 8 * cfg_zoom;
  gfx_width = cfg_zoom * GFX_WIDTH;
  gfx_height = cfg_zoom * GFX_HEIGHT;

  newscreen = SDL_SetVideoMode(gfx_width, gfx_height + gfx_status_rows * charheight, gfx_bpp,
      (SDL_FULLSCREEN * cfg_fullscreen)|SDL_ANYFORMAT|SDL_SWSURFACE);
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


void gfx_show_startup_bg(void) {
  char path[1024];
  FILE *f;

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
  SDL_Surface *bg = SDL_LoadBMP(bmpfile);
  if (!bg) return;

  /* Scale to screen if needed */
  if (bg->w != gfx_width || bg->h != gfx_height) {
    /* Simple nearest-neighbor scale by blitting to screen directly
     * SDL 1.2 doesn't have scaling, so we just center/crop */
    SDL_Rect dst;
    dst.x = (gfx_width - bg->w) / 2;
    dst.y = (gfx_height - bg->h) / 2;
    if (dst.x < 0) dst.x = 0;
    if (dst.y < 0) dst.y = 0;
    SDL_BlitSurface(bg, NULL, gfx_screen, &dst);
  } else {
    SDL_BlitSurface(bg, NULL, gfx_screen, NULL);
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
