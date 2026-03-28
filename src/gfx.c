#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "config.h"
#include "paths.h"

unsigned char gfx_0400_buffer[8000];
unsigned char gfx_d800_buffer[8000];
unsigned char *gfx_0400 = gfx_0400_buffer;
unsigned char *gfx_d800 = gfx_d800_buffer;
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
static SDL_bool dirty[25];
static Uint8 fgcolor = 1, bgcolor = 0;
static int font;
static SDL_Surface *fontlist[2];
static SDL_Surface *rawfont[2];
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
static unsigned char color_to_petscii[] = {
  0x90, 0x05, 0x1c, 0x9f,
  0x9c, 0x1e, 0x1f, 0x9e,
  0x81, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b
};


#define drawpixel(X, Y, C) \
  memcpy(((Uint8 *) surface->pixels) + surface->pitch*(Y) + surface->format->BytesPerPixel*(X), \
	 &(C), surface->format->BytesPerPixel)

SDL_Surface *gfx_createfont(SDL_Surface *srcsurface, int zoom) {
  SDL_Surface *tempsurface;
  SDL_Surface *surface;
  Uint8 *sp;
  int c, x, y, z1, z2, col, hzoom;
  Uint32 fg, bg;

  if ((tempsurface = SDL_CreateRGBSurface(SDL_SWSURFACE, 256 * charwidth, 16 * charheight, 8, 0, 0, 0, 0)) == NULL) {
    return(NULL);
  }
  surface = SDL_DisplayFormat(tempsurface);
  SDL_FreeSurface(tempsurface);
  if (surface == NULL) {
    return(NULL);
  }
  SDL_SetPalette(surface, SDL_LOGPAL|SDL_PHYSPAL, palette, 0, 16);

  if (cfg_columns == 40) {
    hzoom = zoom;
  } else {
    hzoom = zoom / 2;
  }

  bg = SDL_MapRGB(surface->format, palette[bgcolor].r, palette[bgcolor].g, palette[bgcolor].b);
  for (col = 0; col < 16; ++col) {
    fg = SDL_MapRGB(surface->format, palette[col].r, palette[col].g, palette[col].b);
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

  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0) {
    printf("Unable to init SDL: %s\n", SDL_GetError());
    return(1);
  }
  atexit(SDL_Quit);
  vidinfo = SDL_GetVideoInfo();
  if ((gfx_bpp = vidinfo->vfmt->BitsPerPixel) < 15) {
    gfx_bpp = 16;
  }
  if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height, gfx_bpp, (SDL_FULLSCREEN * fullscreen)|SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
    printf("Unable to open window: %s\n", SDL_GetError());
    return(1);
  }
  SDL_WM_SetCaption(appname, appname);
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

  path_build_asset(fname, sizeof(fname), "upper.bmp");
  if ((rawfont[0] = gfx_loadfont(fname)) == NULL) {
    printf("Error: %s", fname);
    return(1);
  }
  path_build_asset(fname, sizeof(fname), "lower.bmp");
  if ((rawfont[1] = gfx_loadfont(fname)) == NULL) {
    printf("Error: %s", fname);
    return(1);
  }
  if (
      (fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom)) == NULL ||
      (fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom)) == NULL
      ) {
        printf("gfx_createfont Error");
    return(1);
  }
  font = 1;
  memset(dirty, SDL_FALSE, sizeof(dirty));

  menu_init(gfx_width, gfx_height);
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
}


void gfx_bgcolor(int c) {
  resetcursor();
  if (bgcolor != c) {
    memset(dirty, SDL_TRUE, sizeof(dirty));
    bgcolor = c;
    gfx_destroyfont(fontlist[0]);
    gfx_destroyfont(fontlist[1]);
    fontlist[0] = gfx_createfont(rawfont[0], cfg_zoom);
    fontlist[1] = gfx_createfont(rawfont[1], cfg_zoom);
  }
}


void gfx_fgcolor(int c) {
  resetcursor();
  fgcolor = c;
}


void gfx_draw_char(int c) {
  resetcursor();
  gfx_0400[gfx_cursy * cfg_columns + gfx_cursx] = c;
  gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = fgcolor;
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


void gfx_cls(void) {
  memset(gfx_0400, 32, cfg_columns * 25);
  memset(gfx_d800, fgcolor, cfg_columns * 25);
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


void gfx_scrollup(void) {
  memmove(gfx_0400_buffer, gfx_0400_buffer + cfg_columns, sizeof(gfx_0400_buffer) - (26 - cfg_rows) * cfg_columns);
  memmove(gfx_d800_buffer, gfx_d800_buffer + cfg_columns, sizeof(gfx_d800_buffer) - (26 - cfg_rows) * cfg_columns);
  memset(gfx_0400 + (cfg_rows - 1) * cfg_columns, 32, cfg_columns);
  memset(gfx_d800 + (cfg_rows - 1) * cfg_columns, fgcolor, cfg_columns);
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
  unsigned char converted[80 * 3 + 1]; // max 3 bytes per char
  FILE *f_screen;
    //Added RoxburySoftware - Defaults to user home dir
    char fname [1024];//getenv("HOME");
  int row, lastcolor = 256, reverse = 0;

  resetcursor();
    //Added RoxburySoftware
    strncpy(fname, cfg_homedir, 1000);
    //Defaults to /Users/Username/Downloads dir
    strcat(fname, "/Downloads/");
    strcat(fname, filename);
    printf("Saving file: %s", fname);
  //if ((f_screen = fopen(filename, "wb"))) {
    if ((f_screen = fopen(fname, "wb"))) {
    for (row = 0; row < 25; ++row) {
      gfx_conv_screen_to_pet(gfx_0400_buffer + gfx_offset + row * cfg_columns, gfx_d800_buffer + gfx_offset + row * cfg_columns, converted, &lastcolor, &reverse, row == 24 ? 0 : 1, cfg_columns);
      fputs((const char *)converted, f_screen);
    }
    fclose(f_screen);
  } else {
    printf("Couldn't open %s for writing\n", filename);
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


void gfx_vbl(void) {
  int first, last, l;

  cursorctr = (cursorctr + 1) % cursorspeed;
  if (cursorctr == cursorspeed/2) {
    invertcursor();
    cursorvis = SDL_TRUE;
    colorundercursor = gfx_d800[gfx_cursy * cfg_columns + gfx_cursx];
    gfx_d800[gfx_cursy * cfg_columns + gfx_cursx] = fgcolor;
  } else if (cursorctr == 0) {
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

}


void gfx_setcursxy(int x, int y) {
  resetcursor();
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

  resetcursor();
  if (gfx_cursx) {
    for (x = gfx_cursx - 1; x < cfg_columns - 1; ++x) {
      rowchar[x] = rowchar[x + 1];
      rowcolor[x] = rowcolor[x + 1];
    }
    rowchar[x] = 32;
    rowcolor[x] = fgcolor;
    --gfx_cursx;
    dirty[gfx_cursy] = SDL_TRUE;
  } else {
    if (gfx_cursy) {
      gfx_cursx = cfg_columns - 1;
      --gfx_cursy;
      gfx_draw_char(32);
    }
    dirty[gfx_cursy - 1] = SDL_TRUE;
  }
}


void gfx_insert(void) {
  int x;
  unsigned char *rowchar = &gfx_0400[gfx_cursy * cfg_columns];
  unsigned char *rowcolor = &gfx_d800[gfx_cursy * cfg_columns];

  resetcursor();
  if (gfx_cursx < cfg_columns - 1) {
    if (rowchar[cfg_columns - 1] == 32) {
      for (x = cfg_columns - 1; x > gfx_cursx; --x) {
	rowchar[x] = rowchar[x - 1];
	rowcolor[x] = rowcolor[x - 1];
      }
      rowchar[x] = 32;
      rowcolor[x] = fgcolor;
      dirty[gfx_cursy] = SDL_TRUE;
    }
  }
}


void gfx_toggle_fullscreen(void) {
  //SDL_WM_ToggleFullScreen(gfx_screen);
  if (cfg_fullscreen) {
    if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height, gfx_bpp, SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
      printf("Unable to open window: %s\n", SDL_GetError());
      exit(1);
    }
    cfg_fullscreen = 0;
  } else {
    if ((gfx_screen = SDL_SetVideoMode(gfx_width, gfx_height, gfx_bpp, SDL_FULLSCREEN|SDL_ANYFORMAT|SDL_SWSURFACE)) == NULL) {
      printf("Unable to open window: %s\n", SDL_GetError());
      exit(1);
    }
    cfg_fullscreen = 1;
  }
  memset(dirty, SDL_TRUE, sizeof(dirty));
}


void gfx_set_offset(int offset) {
  gfx_offset = offset;
  memset(dirty, SDL_TRUE, sizeof(dirty));
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
  for (y = 0; y < rect_h; ++y) {
    dirty[rect_y + y] = SDL_TRUE;
    for (x = 0; x < rect_w; ++x) {
      gfx_0400[(rect_y + y) * cfg_columns + rect_x + x] = *rect_0400++;
      gfx_d800[(rect_y + y) * cfg_columns + rect_x + x] = *rect_d800++;
    }
  }
}
