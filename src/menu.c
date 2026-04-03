#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "font.h"
#include "config.h"
#include "paths.h"
#include "gfx.h"
#include "keyboard.h"
#include "menu.h"


SDL_bool menu_visible;
SDL_bool menu_dirty;
SDL_Surface *menu_surface;
SDL_Surface *cursorsurface;
int menu_width, menu_height;
static Uint32 transparent;
static Uint32 bgcolor;
static Uint32 fgcolor;
static Uint32 shadecolor;
static Uint32 hilitecolor;
static Uint32 inputbg;
static Uint32 cursorcolor;
static Uint32 selectcolor;
static Font *menu_font[6];  /* [0]=body, [1]=body(same), [2]=splash/header, [3]=rotozoom, [4]=c64pro, [5]=petscii */

/* Available splash fonts (24x32, 16x16 grid) */
#define NUM_SPLASH_FONTS 11
static const char *splash_font_files[NUM_SPLASH_FONTS] = {
  "fonts/Maesto_grid.bmp",
  "fonts/Blackentina_grid.bmp",
  "fonts/sportifity_grid.bmp",
  "fonts/playfulboxes_grid.bmp",
  "fonts/youregone_grid.bmp",
  "fonts/PXFXDisco_grid.bmp",
  "fonts/c64pro_large.bmp",
  "fonts/editundo_grid.bmp",
  "fonts/edundot_grid.bmp",
  "fonts/NanoPix_grid.bmp",
  "fonts/Nihonium113_grid.bmp",
};
static const char *splash_font_names[NUM_SPLASH_FONTS] = {
  "Maesto",
  "Blackentina 4F",
  "Sportifity",
  "Playful Boxes",
  "Youre Gone",
  "PXFX Disco",
  "C64 Pro Mono",
  "Edit Undo",
  "Ed Undot",
  "Nano Pix",
  "Nihonium 113",
};

/* Available menu body fonts (10x12, 16x16 grid) */
#define NUM_MENU_FONTS 10
static const char *menu_font_files[NUM_MENU_FONTS] = {
  "fonts/homespun_grid.bmp",
  "fonts/welbut_grid.bmp",
  "fonts/youregone_10x12.bmp",
  "fonts/blrrpixs_grid.bmp",
  "fonts/racquetball_grid.bmp",
  "fonts/px10_grid.bmp",
  "fonts/c64pro_grid.bmp",
  "fonts/PXFXDisco_10x12.bmp",
  "fonts/editundo_10x12.bmp",
  "fonts/edundot_10x12.bmp",
};
static const char *menu_font_names[NUM_MENU_FONTS] = {
  "Homespun",
  "Welbut",
  "Youre Gone",
  "Blrrpixs",
  "Racquetball",
  "PX10",
  "C64 Pro Mono",
  "PXFX Disco",
  "Edit Undo",
  "Ed Undot",
};


#define drawpixel(X, Y, C) \
  memcpy(((Uint8 *) menu_surface->pixels) + menu_surface->pitch*(Y) + menu_surface->format->BytesPerPixel*(X), \
	 &(C), menu_surface->format->BytesPerPixel)
  /*
  ((unsigned char *)menu_surface->pixels)[menu_surface->pitch*(Y) + menu_surface->format->BytesPerPixel*(X)] = (C)
  */
#define SGN(X) ((X) > 0 ? 1 : ((X) == 0 ? 0 : -1))
#define ABS(X) ((X) > 0 ? (X) : (-X))

void menu_draw_line(int x1, int y1, int x2, int y2, Uint32 color)
{
  int lg_delta, sh_delta, cycle, lg_step, sh_step;

  SDL_LockSurface(menu_surface);

  lg_delta = x2 - x1;
  sh_delta = y2 - y1;
  lg_step = SGN(lg_delta);
  lg_delta = ABS(lg_delta);
  sh_step = SGN(sh_delta);
  sh_delta = ABS(sh_delta);

  if (sh_delta < lg_delta) {
    cycle = lg_delta / 2;
    while (x1 != x2) {
      drawpixel(x1, y1, color);
      cycle += sh_delta;
      if (cycle > lg_delta) {
	cycle -= lg_delta;
	y1 += sh_step;
      }
      x1 += lg_step;
    }
    drawpixel(x1, y1, color);
  }

  cycle = sh_delta / 2;
  while (y1 != y2) {
    drawpixel(x1, y1, color);
    cycle += lg_delta;
    if (cycle > sh_delta) {
      cycle -= sh_delta;
      x1 += lg_step;
    }
    y1 += sh_step;
  }
  drawpixel(x1, y1, color);

  SDL_UnlockSurface(menu_surface);
}


void menu_draw_box(int x1, int y1, int x2, int y2, Uint32 color) {
  menu_draw_line(x1, y1, x2, y1, color);
  menu_draw_line(x1, y1, x1, y2, color);
  menu_draw_line(x1, y2, x2, y2, color);
  menu_draw_line(x2, y1, x2, y2, color);
}


void menu_cls(void) {
  SDL_FillRect(menu_surface, NULL, transparent);
  if (menu_visible) {
    menu_dirty = SDL_TRUE;
  }
}


int menu_init(int width, int height) {
  SDL_Surface *tempsurface;
  char fname[1024];
  menu_width = width;
  menu_height = height;
  menu_visible = SDL_FALSE;

  if ((tempsurface = SDL_CreateRGBSurface(SDL_SWSURFACE|SDL_SRCALPHA, width, height, 32,
					   0x000000ff,
					   0x0000ff00,
					   0x00ff0000,
					   0xff000000)) == NULL) {
    return(1);
  }
  menu_surface = SDL_DisplayFormatAlpha(tempsurface);
  SDL_FreeSurface(tempsurface);
  if (menu_surface == NULL) {
    return(1);
  }
  transparent = SDL_MapRGBA(menu_surface->format, 0, 0, 0, SDL_ALPHA_TRANSPARENT);
  bgcolor = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, 0xf0);
  fgcolor = SDL_MapRGBA(menu_surface->format, 0x40, 0x80, 0xff, SDL_ALPHA_OPAQUE);
  shadecolor = SDL_MapRGBA(menu_surface->format, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
  hilitecolor = SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0xff, SDL_ALPHA_OPAQUE);
  inputbg = SDL_MapRGBA(menu_surface->format, 0x0e, 0x14, 0x28, 0xe0);
  cursorcolor = SDL_MapRGB(menu_surface->format, 0xc0, 0x80, 0xff);
  selectcolor = SDL_MapRGBA(menu_surface->format, 0x20, 0x60, 0xa0, 0xc0);

  font_init(menu_surface);

  /* Menu body font — selected by cfg_menufont */
  {
    int mfi = cfg_menufont;
    if (mfi < 0 || mfi >= NUM_MENU_FONTS) mfi = 0;
    path_build_asset(fname, sizeof(fname), menu_font_files[mfi]);
    if ((menu_font[0] = font_load_font(fname, 10, 12, 16, 16)) == NULL) {
      /* Fallback to original */
      path_build_asset(fname, sizeof(fname), "10x12yellow.bmp");
      if ((menu_font[0] = font_load_font(fname, 10, 12, 32, 4)) == NULL) {
        printf("Couldn't load menu font\n");
        SDL_FreeSurface(menu_surface);
        return(1);
      }
    }
    menu_font[1] = menu_font[0];
  }
  /* Splash/logo font — selected by cfg_splashfont (0..5) */
  {
    int fi = cfg_splashfont;
    if (fi < 0 || fi >= NUM_SPLASH_FONTS) fi = 0;
    path_build_asset(fname, sizeof(fname), splash_font_files[fi]);
    if ((menu_font[2] = font_load_font(fname, 24, 32, 16, 16)) == NULL) {
      printf("Couldn't load %s (splash font, non-fatal)\n", fname);
      menu_font[2] = menu_font[1];
    }
  }

  /* Rotozoom effect font — PXFX Disco at 24x32 */
  path_build_asset(fname, sizeof(fname), "fonts/PXFXDisco_grid.bmp");
  if ((menu_font[3] = font_load_font(fname, 24, 32, 16, 16)) == NULL) {
    menu_font[3] = menu_font[2];
  }

  /* C64 Pro Mono font — for hex dump ASCII column and keyboard test */
  path_build_asset(fname, sizeof(fname), "fonts/c64pro_grid.bmp");
  if ((menu_font[4] = font_load_font(fname, 10, 12, 16, 16)) == NULL) {
    menu_font[4] = menu_font[0];
  }

  /* PETSCII font — 8x8 native C64 pixel size, grid position = PETSCII byte
   * Using lowercase/uppercase mode (E1xx) since most BBSes use this mode */
  path_build_asset(fname, sizeof(fname), "fonts/c64pro_petscii_lower.bmp");
  if ((menu_font[5] = font_load_font(fname, 8, 8, 16, 16)) == NULL) {
    menu_font[5] = menu_font[4];  /* fallback to regular C64 Pro */
  }

  if ((cursorsurface = SDL_CreateRGBSurface(SDL_SWSURFACE, 10, 12, 32,
					   0x000000ff,
					   0x0000ff00,
					   0x00ff0000,
					   0x00000000)) == NULL) {
    printf("Couldn't create cursor surface\n");
    SDL_FreeSurface(menu_surface);
    font_free(menu_font[0]);
    font_free(menu_font[1]);
    return(1);
  }
  SDL_FillRect(cursorsurface, NULL, SDL_MapRGB(cursorsurface->format, 0xc0, 0x80, 0xff));
  SDL_SetAlpha(cursorsurface, SDL_SRCALPHA|SDL_RLEACCEL, 0x80);

  return(0);
}


void menu_show(void) {
  menu_dirty = SDL_TRUE;
  menu_visible = SDL_TRUE;
}


void menu_hide(void) {
  menu_dirty = SDL_TRUE;
  menu_visible = SDL_FALSE;
}


void menu_draw_borderbox(int x1, int y1, int x2, int y2) {
  SDL_Rect r;

  menu_draw_box(x1 - 4, y1 - 4, x2 + 4, y2 + 4, shadecolor);
  menu_draw_box(x1 - 3, y1 - 3, x2 + 3, y2 + 3, fgcolor);
  menu_draw_box(x1 - 2, y1 - 2, x2 + 2, y2 + 2, fgcolor);
  menu_draw_box(x1 - 1, y1 - 1, x2 + 1, y2 + 1, shadecolor);
  r.w = (x2 - x1) + 1;
  r.h = (y2 - y1) + 1;
  r.x = x1;
  r.y = y1;
  SDL_FillRect(menu_surface, &r, bgcolor);
}


void menu_print_key(int line, char *key, char *text) {
  int y;
  int x;

  y = menu_height / 2 - 140 + line * 12;
  x = menu_width / 2 - 135;

  if (key[0] == 0 && text[0] == '-') {
    /* Category header in hot pink */
    font_set_font(menu_font[1]);
    font_draw_string_color(x - 10, y, text, 0xff, 0x40, 0x80);
  } else if (key[0] != 0) {
    font_set_font(menu_font[1]);
    font_draw_string(x - strlen(key) * 5, y, key);
    font_set_font(menu_font[0]);
    font_draw_string(x + 26, y, text);
  }
}


static void menu_draw_item(int x, int y, const char *key, const char *text) {
  if (key[0]) {
    int klen = (int)strlen(key);
    /* "[" in cyan */
    font_set_font(menu_font[0]);
    font_draw_string_color(x, y, "[", 0x00, 0xee, 0xff);
    /* Key letter in white */
    font_set_font(menu_font[1]);
    font_draw_string_color(x + 10, y, key, 0xff, 0xff, 0xff);
    /* "]" in cyan */
    font_set_font(menu_font[0]);
    font_draw_string_color(x + 10 + klen * 10, y, "] ", 0x00, 0xee, 0xff);
    /* text in light blue */
    font_draw_string_color(x + 30 + klen * 10, y, text, 0x60, 0x80, 0xff);
  } else {
    font_set_font(menu_font[0]);
    font_draw_string(x, y, "    ");
    font_draw_string_color(x + 40, y, text, 0x60, 0x80, 0xff);
  }
}

/* Draw section title centered within its quadrant, with colored brackets */
static void menu_draw_section_centered(int left, int right, int y, const char *title) {
  int title_len = (int)strlen(title);
  int total_w = (title_len + 4) * 10;  /* "[ " + title + " ]" */
  int tx = (left + right - total_w) / 2;
  font_set_font(menu_font[0]);
  font_draw_string_color(tx, y, "[ ", 0x00, 0xee, 0xff);
  font_set_font(menu_font[1]);
  font_draw_string_color(tx + 20, y, title, 0xff, 0x40, 0x80);
  font_set_font(menu_font[0]);
  font_draw_string_color(tx + 20 + title_len * 10, y, " ]", 0x00, 0xee, 0xff);
}

void menu_print_menu(struct menu *menu) {
  int cx = menu_width / 2;
  int lx = 15;          /* left column x */
  int rx = cx + 10;     /* right column x */
  int ty;               /* current y */
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);
  Uint32 linecol = SDL_MapRGBA(menu_surface->format, 0x20, 0x40, 0x60, SDL_ALPHA_OPAQUE);

  (void)menu;  /* We draw our own layout */

  SDL_FillRect(menu_surface, NULL, solidbg);

  /* Big "CGTERM" logo — splash font at 2x scale, solid cyan */
  ty = 2;
  {
    const char *logo = "CGTERM";
    int logo_len = (int)strlen(logo);
    int logo_w = logo_len * 24 * 2;  /* 24px per char * 2x scale */
    int logo_x = (menu_width - logo_w) / 2;
    font_set_font(menu_font[2]);
    font_draw_string_color_scaled(logo_x, ty, logo, 0x00, 0xee, 0xff, 2);
  }

  /* Subtitle: "::: 3.0 - EPiC SCENE EDiTiON :::" with ::: in cyan */
  {
    const char *pre = ":::";
    const char *mid = " ELiTE C64 BBS SCENE EDiTiON ";
    const char *post = ":::";
    int mid_len = (int)strlen(mid);
    int total_w = 3 * 10 + mid_len * 10 + 3 * 10;
    int sub_x = (menu_width - total_w) / 2;
    font_set_font(menu_font[0]);
    font_draw_string_color(sub_x, ty + 68, pre, 0x00, 0xee, 0xff);
    font_draw_string_color(sub_x + 30, ty + 68, mid, 0x60, 0x80, 0xff);
    font_draw_string_color(sub_x + 30 + mid_len * 10, ty + 68, post, 0x00, 0xee, 0xff);
  }

  /* Horizontal divider */
  ty = ty + 86;
  menu_draw_line(lx, ty, menu_width - lx, ty, linecol);

  /* Vertical divider */
  menu_draw_line(cx, ty, cx, menu_height - 30, linecol);

  /* Top half — CONNECTION (left) and TRANSFERS & FiLES (right) */
  ty += 8;
  menu_draw_section_centered(lx, cx, ty, "CONNECTION");
  menu_draw_item(lx, ty + 28, "B", "Bookmarks");
  menu_draw_item(lx, ty + 42, "D", "Connect/Disconnect");
  menu_draw_item(lx, ty + 56, "R", "Reconnect");

  menu_draw_section_centered(rx, menu_width - lx, ty, "TRANSFERS & FiLES");
  menu_draw_item(rx, ty + 28, "T", "Transfer file");
  menu_draw_item(rx, ty + 42, "J", "Set path");
  menu_draw_item(rx, ty + 56, "U", "Unjoin image");
  menu_draw_item(rx, ty + 70, "N", "New disk image");

  /* Horizontal divider — middle of screen */
  {
    int mid_y = menu_height / 2;
    menu_draw_line(lx, mid_y, menu_width - lx, mid_y, linecol);

    /* Bottom half — SCREEN & MACROS (left) and SETTINGS (right) */
    ty = mid_y + 8;
    menu_draw_section_centered(lx, cx, ty, "SCREEN & MACROS");
    menu_draw_item(lx, ty + 28, "L", "Load seq file");
    menu_draw_item(lx, ty + 42, "P", "Post SEQ to BBS");
    menu_draw_item(lx, ty + 56, "S", "Save screen");
    menu_draw_item(lx, ty + 70, "I", "Screenshot");
    menu_draw_item(lx, ty + 84, "C", "Record macro");
    menu_draw_item(lx, ty + 98, "V", "Play macro");
    menu_draw_item(lx, ty + 112, "A", "Abort");

    menu_draw_section_centered(rx, menu_width - lx, ty, "SETTINGS");
    menu_draw_item(rx, ty + 28, "E", "Local echo");
    menu_draw_item(rx, ty + 42, "F", "Fullscreen");
    menu_draw_item(rx, ty + 56, "Z", "Font settings");

    /* Oldschool modem toggle — ON in green, OFF in red */
    {
      int mx = rx;
      int my = ty + 70;
      font_set_font(menu_font[0]);
      font_draw_string_color(mx, my, "[", 0x00, 0xee, 0xff);
      font_set_font(menu_font[1]);
      font_draw_string_color(mx + 10, my, "M", 0xff, 0xff, 0xff);
      font_set_font(menu_font[0]);
      font_draw_string_color(mx + 20, my, "] Oldskool Mode ", 0x00, 0xee, 0xff);
      if (cfg_modem)
        font_draw_string_color(mx + 200, my, "[ON]", 0x00, 0xff, 0x66);
      else
        font_draw_string_color(mx + 200, my, "[OFF]", 0xff, 0x40, 0x40);
    }

    menu_draw_item(rx, ty + 84, "K", "Keyboard layout");
    menu_draw_item(rx, ty + 98, "W", "Toggle upper/lowercase");

    /* Terminal mode toggle — PETSCII/ANSI */
    {
      int mx = rx;
      int my = ty + 112;
      font_set_font(menu_font[0]);
      font_draw_string_color(mx, my, "[", 0x00, 0xee, 0xff);
      font_set_font(menu_font[1]);
      font_draw_string_color(mx + 10, my, "G", 0xff, 0xff, 0xff);
      font_set_font(menu_font[0]);
      font_draw_string_color(mx + 20, my, "] Terminal ", 0x00, 0xee, 0xff);
      if (cfg_termmode == 1)
        font_draw_string_color(mx + 140, my, "[ANSI]", 0x00, 0xff, 0x66);
      else
        font_draw_string_color(mx + 140, my, "[PETSCII]", 0xff, 0x80, 0xff);
    }

    /* Character set toggle */
    {
      int mx = rx;
      int my = ty + 126;
      const char *csname = "US/UK";
      if (cfg_charset == 1) csname = "Swedish";
      else if (cfg_charset == 2) csname = "German";
      font_set_font(menu_font[0]);
      font_draw_string_color(mx, my, "[", 0x00, 0xee, 0xff);
      font_set_font(menu_font[1]);
      font_draw_string_color(mx + 10, my, "H", 0xff, 0xff, 0xff);
      font_set_font(menu_font[0]);
      font_draw_string_color(mx + 20, my, "] Charset ", 0x00, 0xee, 0xff);
      font_draw_string_color(mx + 120, my, "[", 0x00, 0xee, 0xff);
      font_draw_string_color(mx + 130, my, csname, 0xff, 0xff, 0x54);
      font_draw_string_color(mx + 130 + (int)strlen(csname) * 10, my, "]", 0x00, 0xee, 0xff);
    }

    menu_draw_item(rx, ty + 154, "Q", "Quit CGTerm");
  }

  /* Transfer path at bottom */
  {
    int py = menu_height - 18;
    char truncpath[55];
    int maxchars = (menu_width - 140) / 10;

    font_set_font(menu_font[0]);
    font_draw_string_color(lx, py, "WAREZ DiR:>", 0x00, 0xee, 0xff);
    if ((int)strlen(cfg_dldir) > maxchars) {
      snprintf(truncpath, sizeof(truncpath), "...%s", cfg_dldir + strlen(cfg_dldir) - maxchars + 3);
    } else {
      snprintf(truncpath, sizeof(truncpath), "%s", cfg_dldir);
    }
    font_draw_string_color(lx + 120, py, truncpath, 0x60, 0x80, 0xff);
  }

  /* Border */
  menu_draw_box(5, 5, menu_width - 6, menu_height - 6,
    SDL_MapRGBA(menu_surface->format, 0x40, 0x80, 0xff, SDL_ALPHA_OPAQUE));
}


void menu_update_input(const char *text, int cursorpos) {
  SDL_Rect r;
  int cy = menu_height / 2;

  r.w = menu_width - 40;
  r.h = 14;
  r.x = 21;
  r.y = cy + 3;
  SDL_FillRect(menu_surface, &r, inputbg);
  font_set_font(menu_font[0]);
  font_draw_string(22, cy + 4, text);
  menu_dirty = SDL_TRUE;
  r.x = cursorpos * 10 + 22;
  r.y = cy + 4;
  SDL_BlitSurface(cursorsurface, NULL, menu_surface, &r);
}


void menu_draw_input(const char *title) {
  int cy = menu_height / 2;
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);
  SDL_FillRect(menu_surface, NULL, solidbg);
  menu_draw_borderbox(15, cy - 20, menu_width - 16, cy + 20);
  font_set_font(menu_font[0]);
  font_draw_string(22, cy - 16, title);
}


void menu_update_xfer(int direction, int protocol) {
  SDL_Rect r;

  r.x = 68;
  r.y = 8;
  r.w = menu_width - 68 * 2;
  r.h = menu_height - 8 * 2;
  SDL_FillRect(menu_surface, &r, bgcolor);

  r.x = 80;
  r.w = 160;
  r.h = 13;

  if (direction) {
    r.y = 37 - 12 + direction * 12;
    SDL_FillRect(menu_surface, &r, cursorcolor);
    menu_dirty = SDL_TRUE;
  }

  if (protocol) {
    r.y = 101 - 12 + protocol * 12;
    SDL_FillRect(menu_surface, &r, cursorcolor);
    menu_dirty = SDL_TRUE;
  }

  font_set_font(menu_font[1]);
  font_draw_string(70, 19, "    Direction");
  font_draw_string(80, 38, " S  ");
  font_draw_string(80, 50, " R  ");
  font_set_font(menu_font[0]);
  font_draw_string(112, 38, "Send file");
  font_draw_string(112, 50, "Receive file");

  font_set_font(menu_font[1]);
  font_draw_string(70, 83, "    Protocol");
  font_draw_string(80, 102, " X  ");
  font_draw_string(80, 114, " C  ");
  font_draw_string(80, 126, " 1  ");
  font_draw_string(80, 138, " P  ");
  font_draw_string(80, 150, " W  ");
  font_draw_string(80, 162, " M  ");
  font_set_font(menu_font[0]);
  font_draw_string(112, 102, "Xmodem");
  font_draw_string(112, 114, "Xmodem/CRC");
  font_draw_string(112, 126, "Xmodem-1k");
  font_draw_string(112, 138, "Punter");
  font_draw_string(112, 150, "Rainbow");
  font_draw_string(112, 162, "Multi Punter");

  font_set_font(menu_font[1]);
  font_draw_string(84, 183, "Return");
  font_set_font(menu_font[0]);
  font_draw_string(154, 183, "to start");
}


void menu_draw_xfer(void) {
  menu_cls();
  menu_draw_borderbox(67, 7, menu_width - 68, menu_height - 8);
  menu_update_xfer(0, 0);
}


char *proto[] = {
  "",
  "Xmodem",
  "Xmodem/CRC",
  "Xmodem-1k",
  "Punter",
  "Rainbow",
  "Multi Punter"
};

char *dir[] = {
  "",
  "send",
  "receive"
};


/* Stored state for the transfer progress display */
static char xfer_disp_filename[32];
static char xfer_disp_direction[16];
static char xfer_disp_protocol[16];

/* PETSCII preview mode for SEQ posting */
static int xfer_seq_preview_mode = 0;
static int seq_cx = 0, seq_cy = 0;  /* cursor position */
static int seq_color = 1;  /* current C64 color (default white) */
static int seq_reverse = 0;

/* Raw data ring buffer for hex dump display */
#define XFER_HEXDUMP_SIZE 256
static unsigned char xfer_hexbuf[XFER_HEXDUMP_SIZE];
static int xfer_hexpos = 0;
static int xfer_hexcount = 0;

static int xfer_anim_direction = 0;
static int xfer_anim_frame = 0;

void menu_xfer_set_seq_preview(int enabled) {
  xfer_seq_preview_mode = enabled;
  seq_cx = 0; seq_cy = 0;
  seq_color = 1; seq_reverse = 0;
}

void menu_xfer_feed_byte(unsigned char b) {
  xfer_hexbuf[xfer_hexpos] = b;
  xfer_hexpos = (xfer_hexpos + 1) % XFER_HEXDUMP_SIZE;
  if (xfer_hexcount < XFER_HEXDUMP_SIZE) xfer_hexcount++;

  /* In SEQ preview mode, render each byte immediately */
  if (xfer_seq_preview_mode) {
    static const int c64_r[] = {0x00,0xff,0x88,0x70,0x88,0x35,0x20,0xcf,0x88,0x55,0xbb,0x55,0x77,0x70,0x58,0x99};
    static const int c64_g[] = {0x00,0xff,0x00,0xa4,0x00,0xa8,0x00,0xd8,0x54,0x43,0x44,0x55,0x77,0xff,0x6f,0x99};
    static const int c64_b[] = {0x00,0xff,0x00,0xb7,0xa8,0x00,0xcc,0x00,0x1e,0x00,0x44,0x55,0x77,0x78,0xff,0x99};
    int lx = 15;
    int cols = 40;
    int max_rows = (menu_height - 210) / 8;

    /* Color control codes */
    switch (b) {
      case 0x05: seq_color = 1; return;
      case 0x1c: seq_color = 2; return;
      case 0x1e: seq_color = 5; return;
      case 0x1f: seq_color = 6; return;
      case 0x81: seq_color = 8; return;
      case 0x90: seq_color = 0; return;
      case 0x95: seq_color = 9; return;
      case 0x96: seq_color = 10; return;
      case 0x97: seq_color = 11; return;
      case 0x98: seq_color = 12; return;
      case 0x99: seq_color = 13; return;
      case 0x9a: seq_color = 14; return;
      case 0x9b: seq_color = 15; return;
      case 0x9c: seq_color = 4; return;
      case 0x9e: seq_color = 7; return;
      case 0x9f: seq_color = 3; return;
      case 0x12: seq_reverse = 1; return;
      case 0x92: seq_reverse = 0; return;
      case 0x0d: seq_cx = 0; seq_cy++; return;
      case 0x11: seq_cy++; return;
      case 0x91: if (seq_cy > 0) seq_cy--; return;
      case 0x1d: seq_cx++; return;
      case 0x9d: if (seq_cx > 0) seq_cx--; return;
      case 0x13: seq_cx = 0; seq_cy = 0; return;
      case 0x14: if (seq_cx > 0) seq_cx--; return;
      default: break;
    }

    if (b < 0x20) return;  /* skip other control chars */

    /* Scroll if past bottom */
    if (seq_cy >= max_rows) {
      /* TODO: could scroll the surface, for now just wrap */
      seq_cy = 0;
      /* Clear preview area */
      SDL_Rect clr;
      clr.x = lx; clr.y = 154; clr.w = menu_width - lx * 2; clr.h = max_rows * 8;
      SDL_FillRect(menu_surface, &clr, bgcolor);
    }

    /* Draw the character using PETSCII font (grid position = byte value) */
    if (seq_cx < cols && seq_cy < max_rows) {
      int px = lx + seq_cx * 8;
      int py = 154 + seq_cy * 8;
      int r = c64_r[seq_color], g = c64_g[seq_color], bl = c64_b[seq_color];
      char ch[2];
      ch[1] = 0;
      ch[0] = (char)b;  /* byte value = grid position in PETSCII font */

      font_set_font(menu_font[5]);  /* PETSCII font */
      if (seq_reverse) {
        SDL_Rect rbg;
        rbg.x = px; rbg.y = py; rbg.w = 8; rbg.h = 8;
        SDL_FillRect(menu_surface, &rbg,
          SDL_MapRGBA(menu_surface->format, r, g, bl, SDL_ALPHA_OPAQUE));
        font_draw_string_color(px, py, ch, 0x00, 0x00, 0x00);
      } else {
        font_draw_string_color(px, py, ch, r, g, bl);
      }

      seq_cx++;
      if (seq_cx >= cols) {
        seq_cx = 0;
        seq_cy++;
      }
      menu_dirty = SDL_TRUE;
    }
  }
}

void menu_update_xfer_progress(const char *message, int bytes, int total) {
  SDL_Rect r;
  char line[80];
  int size;
  int blocks;
  static Uint32 xfer_start_ticks = 0;
  static int xfer_started = 0;
  Uint32 now;
  int speed_bps = 0;
  int lx = 15;
  int bar_w = menu_width - 30;
  Uint32 linecol = SDL_MapRGBA(menu_surface->format, 0x20, 0x40, 0x60, SDL_ALPHA_OPAQUE);

  if (bytes < 0) bytes = 0;
  if (total < 0) total = 0;
  if (total > 0 && bytes > total) bytes = total;

  now = SDL_GetTicks();
  if (!xfer_started || bytes == 0) {
    xfer_start_ticks = now;
    xfer_started = 1;
    xfer_hexcount = 0;
    xfer_hexpos = 0;
  }
  {
    Uint32 elapsed = now - xfer_start_ticks;
    if (elapsed > 500 && bytes > 0) {
      speed_bps = (int)((long)bytes * 1000 / elapsed);
    }
  }

  /* Clear dynamic area — only clear above preview in SEQ mode */
  r.x = 8; r.y = 70; r.w = menu_width - 16;
  r.h = xfer_seq_preview_mode ? 66 : (menu_height - 110);
  SDL_FillRect(menu_surface, &r, bgcolor);

  font_set_font(menu_font[0]);

  /* Filename — cyan label, white value */
  font_draw_string_color(lx, 74, "FiLE:", 0x00, 0xee, 0xff);
  font_draw_string_color(lx + 60, 74, xfer_disp_filename, 0xff, 0xff, 0xff);

  /* Block count — cyan label, neon green value */
  blocks = (bytes + 253) / 254;
  if (total > 0) {
    int total_blocks = (total + 253) / 254;
    snprintf(line, sizeof(line), "%d / %d", blocks, total_blocks);
  } else {
    snprintf(line, sizeof(line), "%d", blocks);
  }
  font_draw_string_color(lx, 88, "BLOCKS:", 0x00, 0xee, 0xff);
  font_draw_string_color(lx + 80, 88, line, 0x00, 0xff, 0x66);

  /* Bytes + speed — cyan label, values in different colors */
  if (speed_bps > 0) {
    snprintf(line, sizeof(line), "%d bytes  (%d B/s)", bytes, speed_bps);
  } else {
    snprintf(line, sizeof(line), "%d bytes", bytes);
  }
  font_draw_string_color(lx, 102, "DATA:", 0x00, 0xee, 0xff);
  font_draw_string_color(lx + 60, 102, line, 0xff, 0xff, 0xff);

  /* Progress bar — full width */
  r.x = lx; r.y = 118; r.w = bar_w; r.h = 12;
  SDL_FillRect(menu_surface, &r, inputbg);

  if (total > 0) {
    size = (bar_w * bytes) / total;
  } else {
    size = bytes % bar_w;
  }
  if (size < 0) size = 0;
  if (size > bar_w) size = bar_w;
  if (size > 0) {
    r.w = size;
    SDL_FillRect(menu_surface, &r,
      SDL_MapRGBA(menu_surface->format, 0x00, 0xcc, 0xff, SDL_ALPHA_OPAQUE));
  }

  /* Percentage text on the bar */
  if (total > 0) {
    int pct = (int)((long)bytes * 100 / total);
    snprintf(line, sizeof(line), "%d%%", pct);
    font_draw_string_color(lx + bar_w / 2 - 15, 119, line, 0xff, 0xff, 0xff);
  }

  /* Transfer animation — pixel art C64 + floppy with animated data flow */
  {
    int ax = menu_width - 185;
    int ay = 52;
    int f = xfer_anim_frame % 12;
    SDL_Rect icon;
    Uint32 c64col = SDL_MapRGBA(menu_surface->format, 0x60, 0x80, 0xff, SDL_ALPHA_OPAQUE);
    Uint32 floppycol = SDL_MapRGBA(menu_surface->format, 0xff, 0x40, 0x80, SDL_ALPHA_OPAQUE);
    Uint32 datacol = SDL_MapRGBA(menu_surface->format, 0x00, 0xff, 0x66, SDL_ALPHA_OPAQUE);
    Uint32 darkcol = SDL_MapRGBA(menu_surface->format, 0x20, 0x30, 0x50, SDL_ALPHA_OPAQUE);
    int left_x, right_x;

    /* Draw C64 computer icon (left or right depending on direction) */
    /* Simple: monitor shape = rectangle with screen inside */
    if (xfer_anim_direction == 0) {
      /* Download: modem on left, floppy on right */
      left_x = ax; right_x = ax + 130;
    } else {
      /* Upload: floppy on left, modem on right */
      left_x = ax; right_x = ax + 130;
    }

    /* Left icon: modem/PC (download) or floppy (upload) */
    if (xfer_anim_direction == 0) {
      /* PC/Modem: monitor shape */
      icon.x = left_x; icon.y = ay; icon.w = 30; icon.h = 20;
      SDL_FillRect(menu_surface, &icon, c64col);
      icon.x = left_x+2; icon.y = ay+2; icon.w = 26; icon.h = 14;
      SDL_FillRect(menu_surface, &icon, darkcol);
      icon.x = left_x+5; icon.y = ay+20; icon.w = 20; icon.h = 3;
      SDL_FillRect(menu_surface, &icon, c64col);
      font_set_font(menu_font[0]);
      font_draw_string_color(left_x+1, ay+22, "PC", 0x60, 0x80, 0xff);
    } else {
      /* 1541 floppy: box with slot */
      icon.x = left_x; icon.y = ay; icon.w = 35; icon.h = 22;
      SDL_FillRect(menu_surface, &icon, floppycol);
      icon.x = left_x+3; icon.y = ay+3; icon.w = 29; icon.h = 8;
      SDL_FillRect(menu_surface, &icon, darkcol);
      icon.x = left_x+12; icon.y = ay+15; icon.w = 6; icon.h = 4;
      SDL_FillRect(menu_surface, &icon, datacol); /* LED */
      font_set_font(menu_font[0]);
      font_draw_string_color(left_x-2, ay+22, "1541", 0xff, 0x40, 0x80);
    }

    /* Right icon: floppy (download) or PC (upload) */
    if (xfer_anim_direction == 0) {
      /* 1541 floppy */
      icon.x = right_x; icon.y = ay; icon.w = 35; icon.h = 22;
      SDL_FillRect(menu_surface, &icon, floppycol);
      icon.x = right_x+3; icon.y = ay+3; icon.w = 29; icon.h = 8;
      SDL_FillRect(menu_surface, &icon, darkcol);
      icon.x = right_x+12; icon.y = ay+15; icon.w = 6; icon.h = 4;
      SDL_FillRect(menu_surface, &icon, datacol);
      font_set_font(menu_font[0]);
      font_draw_string_color(right_x-2, ay+22, "1541", 0xff, 0x40, 0x80);
    } else {
      /* PC/Modem */
      icon.x = right_x; icon.y = ay; icon.w = 30; icon.h = 20;
      SDL_FillRect(menu_surface, &icon, c64col);
      icon.x = right_x+2; icon.y = ay+2; icon.w = 26; icon.h = 14;
      SDL_FillRect(menu_surface, &icon, darkcol);
      icon.x = right_x+5; icon.y = ay+20; icon.w = 20; icon.h = 3;
      SDL_FillRect(menu_surface, &icon, c64col);
      font_set_font(menu_font[0]);
      font_draw_string_color(right_x+1, ay+22, "PC", 0x60, 0x80, 0xff);
    }

    /* Animated data packets flowing between the icons */
    {
      int pi, packet_x;
      int gap = right_x - left_x - 35;
      for (pi = 0; pi < 3; pi++) {
        int phase = (f + pi * 4) % 12;
        packet_x = left_x + 35 + (gap * phase) / 12;
        if (xfer_anim_direction == 1) {
          /* Upload: reverse direction */
          packet_x = right_x - (gap * phase) / 12;
        }
        icon.x = packet_x; icon.y = ay + 8; icon.w = 6; icon.h = 4;
        SDL_FillRect(menu_surface, &icon, datacol);
      }
    }

    xfer_anim_frame++;
  }

  /* Horizontal divider */
  menu_draw_line(lx, 136, menu_width - lx, 136, linecol);

  if (xfer_seq_preview_mode) {
    /* PETSCII preview — characters drawn in real-time by menu_xfer_feed_byte.
     * Only draw the title, don't touch the preview area below. */
    font_draw_string_color(lx, 140, "PETSCii PREViEW:", 0xff, 0x40, 0x80);
  } else {

  /* Raw hex dump title */
  font_draw_string_color(lx, 140, "RAW DATA:", 0xff, 0x40, 0x80);

  /* Hex dump — body font for hex, C64 Pro for ASCII column */
  font_set_font(menu_font[0]);
  if (xfer_hexcount > 0) {
    int row, col;
    int max_rows = (menu_height - 210) / 12;
    /* 30px per hex byte + 10px per ascii char + offset(55) + margins */
    int bytes_per_row = (menu_width - 85) / 40;
    int shown = 0;
    int total_to_show = xfer_hexcount;
    int start_idx;

    if (bytes_per_row > 16) bytes_per_row = 16;
    if (bytes_per_row < 4) bytes_per_row = 4;
    if (max_rows < 1) max_rows = 1;
    if (total_to_show > max_rows * bytes_per_row)
      total_to_show = max_rows * bytes_per_row;

    start_idx = (xfer_hexpos - total_to_show + XFER_HEXDUMP_SIZE) % XFER_HEXDUMP_SIZE;

    for (row = 0; row < max_rows && shown < total_to_show; row++) {
      int hy = 154 + row * 12;
      char hex_part[80];
      char asc_part[20];
      int hp = 0, ap = 0;

      memset(hex_part, 0, sizeof(hex_part));
      memset(asc_part, 0, sizeof(asc_part));

      /* Offset */
      snprintf(line, sizeof(line), "%04X:", (bytes - total_to_show + shown) & 0xFFFF);
      font_draw_string_color(lx, hy, line, 0x60, 0x60, 0x80);

      for (col = 0; col < bytes_per_row && shown < total_to_show; col++) {
        unsigned char b = xfer_hexbuf[(start_idx + shown) % XFER_HEXDUMP_SIZE];
        hp += snprintf(hex_part + hp, sizeof(hex_part) - hp, "%02X ", b);
        if (ap < 17) asc_part[ap++] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
        shown++;
      }
      asc_part[ap] = 0;

      font_set_font(menu_font[0]);
      font_draw_string_color(lx + 55, hy, hex_part, 0x60, 0x80, 0xff);
      /* ASCII column in C64 Pro font */
      {
        int asc_x = menu_width - (bytes_per_row * 10) - 15;
        font_set_font(menu_font[4]);
        font_draw_string_color(asc_x, hy, asc_part, 0x00, 0xff, 0x66);
        font_set_font(menu_font[0]);
      }
    }
  }
  } /* end else (hex dump mode) */

  /* Restore body font for hints */
  font_set_font(menu_font[0]);

  /* ESC hint — inside the frame */
  font_draw_string_color(lx, menu_height - 54, "Esc ", 0xff, 0x40, 0x80);
  font_draw_string_color(lx + 40, menu_height - 54, "cancel transfer", 0x60, 0x80, 0xff);

  menu_dirty = SDL_TRUE;
}



/* Redirect block-based progress to the unified progress display */
void menu_update_xfer_block_progress(const char *status, const char *protocol, int current_blocks, int total_blocks) {
  int current_bytes = current_blocks * 254;
  int total_bytes = total_blocks > 0 ? total_blocks * 254 : 0;
  (void)status;
  (void)protocol;
  menu_update_xfer_progress("Transferring...", current_bytes, total_bytes);
}

void menu_draw_xfer_progress(const char *filename, int direction, int protocol) {
  char s[64];

  /* Store state for update calls */
  snprintf(xfer_disp_filename, sizeof(xfer_disp_filename), "%s", filename);
  snprintf(xfer_disp_direction, sizeof(xfer_disp_direction), "%s", dir[direction]);
  snprintf(xfer_disp_protocol, sizeof(xfer_disp_protocol), "%s", proto[protocol]);
  xfer_anim_direction = direction;
  xfer_anim_frame = 0;

  menu_cls();
  menu_draw_borderbox(7, 47, menu_width - 8, menu_height - 38);

  /* [protocol]: uploading/downloading — colored */
  font_set_font(menu_font[0]);
  font_draw_string_color(15, 52, "[", 0x00, 0xee, 0xff);
  font_draw_string_color(25, 52, proto[protocol], 0xff, 0x40, 0x80);
  snprintf(s, sizeof(s), "]: %sing",
    direction == 1 ? "upload" : "download");
  font_draw_string_color(25 + (int)strlen(proto[protocol]) * 10, 52, s, 0x00, 0xee, 0xff);
}


void menu_draw_rectangle(void) {
  menu_cls();
  menu_draw_borderbox(67, 57, menu_width - 68, menu_height - 48);
  font_set_font(menu_font[1]);
  font_draw_string(108, 70, "Rectangle");
  font_draw_string(80, 90, " S  ");
  font_draw_string(80, 102, " X  ");
  font_draw_string(80, 114, " C  ");
  font_draw_string(80, 126, " V  ");
  font_set_font(menu_font[0]);
  font_draw_string(112, 90, "Set corner");
  font_draw_string(112, 102, "Cut");
  font_draw_string(112, 114, "Copy");
  font_draw_string(112, 126, "Paste");
}


#include <math.h>

/* ------------------------------------------------------------------ */
/*  Demo-style splash screen                                          */
/* ------------------------------------------------------------------ */

#define SPLASH_NUM_STARS 60

static struct { float x, y, z; } splash_stars[SPLASH_NUM_STARS];
static int splash_stars_inited = 0;

static unsigned int splash_rng_state = 12345;
static unsigned int splash_rand(void) {
  splash_rng_state ^= splash_rng_state << 13;
  splash_rng_state ^= splash_rng_state >> 17;
  splash_rng_state ^= splash_rng_state << 5;
  return splash_rng_state;
}

static void splash_init_stars(void) {
  int i;
  for (i = 0; i < SPLASH_NUM_STARS; i++) {
    splash_stars[i].x = (float)((int)(splash_rand() % 320) - 160);
    splash_stars[i].y = (float)((int)(splash_rand() % 200) - 100);
    splash_stars[i].z = (float)(splash_rand() % 256 + 1);
  }
  splash_stars_inited = 1;
}

void menu_draw_splash_frame(int frame, const char *dlpath, const char *ulpath) {
  SDL_Rect r;
  int i, sx, sy;
  float t = frame * 0.02f;
  int w = menu_width;
  int h = menu_height;
  Uint32 black = SDL_MapRGBA(menu_surface->format, 0x05, 0x08, 0x15, 0xf0);

  if (!splash_stars_inited) splash_init_stars();

  /* Dark blue background */
  {
    Uint32 bg = SDL_MapRGBA(menu_surface->format, 0x08, 0x08, 0x30, SDL_ALPHA_OPAQUE);
    SDL_FillRect(menu_surface, NULL, bg);
  }

  /* Raster bars */
  SDL_LockSurface(menu_surface);
  for (i = 0; i < h; i++) {
    float wave = sinf(t * 3.0f + i * 0.04f) * 0.5f + 0.5f;
    int rr = (int)(wave * 40);
    int gg = (int)(wave * 20);
    int bb = (int)(60 + wave * 40);
    Uint32 col = SDL_MapRGBA(menu_surface->format, rr, gg, bb, SDL_ALPHA_OPAQUE);
    r.x = 0; r.y = i; r.w = w; r.h = 1;
    SDL_FillRect(menu_surface, &r, col);
  }
  SDL_UnlockSurface(menu_surface);

  /* Starfield */
  SDL_LockSurface(menu_surface);
  for (i = 0; i < SPLASH_NUM_STARS; i++) {
    int bright;
    Uint32 scol;
    splash_stars[i].z -= 1.2f;
    if (splash_stars[i].z <= 0) {
      splash_stars[i].x = (float)((int)(splash_rand() % 320) - 160);
      splash_stars[i].y = (float)((int)(splash_rand() % 200) - 100);
      splash_stars[i].z = 256.0f;
    }
    sx = (int)(splash_stars[i].x * 256.0f / splash_stars[i].z) + w / 2;
    sy = (int)(splash_stars[i].y * 256.0f / splash_stars[i].z) + h / 2;
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
    bright = (int)(255 - splash_stars[i].z);
    if (bright < 40) bright = 40;
    if (bright > 255) bright = 255;
    scol = SDL_MapRGBA(menu_surface->format, bright, bright, bright, SDL_ALPHA_OPAQUE);
    drawpixel(sx, sy, scol);
  }
  SDL_UnlockSurface(menu_surface);

  /* ---- Cycling demo effects in center area ---- */
  {
    /* Effect area: below title, above scroller */
    int ey_top = 55;
    int ey_bot = h - 90;
    int eh = ey_bot - ey_top;
    int ecx = w / 2;
    int ecy = ey_top + eh / 2;

    /* 5 effects, ~300 frames (~6 sec) each, last 40 frames = transition */
    #define NUM_DEMO_FX 4
    #define FX_PERIOD 300
    #define FX_TRANS 40
    int fx_idx = (frame / FX_PERIOD) % NUM_DEMO_FX;
    int fx_local = frame % FX_PERIOD;
    int fx_next = (fx_idx + 1) % NUM_DEMO_FX;
    int in_transition = (fx_local >= FX_PERIOD - FX_TRANS);
    float trans_alpha = in_transition ? (float)(fx_local - (FX_PERIOD - FX_TRANS)) / (float)FX_TRANS : 0.0f;

    /* Helper: render one effect into the effect area.
     * We call this once for the current effect, and during transitions
     * we darken/brighten to create a crossfade. */
    int fx_pass;
    for (fx_pass = 0; fx_pass < (in_transition ? 2 : 1); fx_pass++) {
      int cur_fx = (fx_pass == 0) ? fx_idx : fx_next;
      float fade = 1.0f;
      if (in_transition) {
        fade = (fx_pass == 0) ? (1.0f - trans_alpha) : trans_alpha;
      }

    switch (cur_fx) {

    /* --- Effect 0: Vectorballs (3D rotating sphere of dots) — BIG --- */
    case 0: {
      #define VBALL_COUNT 80
      float ca = cosf(t * 0.8f), sa = sinf(t * 0.8f);
      float cb = cosf(t * 0.5f), sb = sinf(t * 0.5f);
      int bi;

      SDL_LockSurface(menu_surface);
      for (bi = 0; bi < VBALL_COUNT; bi++) {
        /* Fibonacci sphere distribution */
        float phi_fib = acosf(1.0f - 2.0f * (bi + 0.5f) / VBALL_COUNT);
        float theta = 3.14159f * (1.0f + sqrtf(5.0f)) * bi;
        float bx = sinf(phi_fib) * cosf(theta) * 4.5f;
        float by = sinf(phi_fib) * sinf(theta) * 4.5f;
        float bz = cosf(phi_fib) * 4.5f;
        float rx, rz, ry, rz2;
        float scale;
        int spx, spy, bsize, bright;
        float hue;
        int rr, gg, bb;

        /* Rotate Y then X */
        rx = bx * ca - bz * sa; rz = bx * sa + bz * ca;
        ry = by * cb - rz * sb; rz2 = by * sb + rz * cb;

        scale = 90.0f / (rz2 + 8.0f);
        spx = (int)(rx * scale) + ecx;
        spy = (int)(ry * scale) + ecy;
        if (spx < 8 || spx >= w - 8 || spy < ey_top || spy >= ey_bot) continue;

        /* Size by depth — 3x bigger */
        bsize = (rz2 > 0) ? 8 : 5;
        bright = (int)(200 + rz2 * 20);
        if (bright < 80) bright = 80;
        if (bright > 255) bright = 255;

        hue = fmodf(t * 0.3f + (float)bi * 0.02f, 1.0f);
        rr = (int)((sinf(hue * 6.28f) * 0.5f * bright + bright * 0.5f) * fade);
        gg = (int)((sinf(hue * 6.28f + 2.09f) * 0.5f * bright + bright * 0.5f) * fade);
        bb = (int)((sinf(hue * 6.28f + 4.19f) * 0.5f * bright + bright * 0.5f) * fade);
        if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
        if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
        if (bb < 0) bb = 0; else if (bb > 255) bb = 255;

        /* Draw a filled circle — shaded sphere-like ball */
        { int dx, dy;
          for (dy = -bsize; dy <= bsize; dy++)
            for (dx = -bsize; dx <= bsize; dx++) {
              float dist2 = (float)(dx*dx + dy*dy);
              float rad2 = (float)(bsize*bsize);
              if (dist2 <= rad2) {
                /* Shading: brighter toward top-left (light source) */
                float shade = 1.0f - 0.4f * (dist2 / rad2)
                             + 0.2f * (float)(-dx - dy) / (float)bsize;
                if (shade < 0.3f) shade = 0.3f;
                if (shade > 1.0f) shade = 1.0f;
                int sr = (int)(rr * shade);
                int sg = (int)(gg * shade);
                int sb2 = (int)(bb * shade);
                Uint32 scol = SDL_MapRGBA(menu_surface->format,
                  sr > 255 ? 255 : sr, sg > 255 ? 255 : sg, sb2 > 255 ? 255 : sb2,
                  SDL_ALPHA_OPAQUE);
                int ppx = spx + dx, ppy = spy + dy;
                if (ppx >= 0 && ppx < w && ppy >= ey_top && ppy < ey_bot)
                  drawpixel(ppx, ppy, scol);
              }
            }
        }
      }
      SDL_UnlockSurface(menu_surface);
      break;
    }

    /* --- Effect 1: Full-screen plasma --- */
    case 1: {
      int px, py;
      SDL_LockSurface(menu_surface);
      for (py = ey_top; py < ey_bot; py += 4) {
        for (px = 0; px < w; px += 4) {
          float fx2 = (float)px / (float)w;
          float fy = (float)(py - ey_top) / (float)eh;
          float v1 = sinf(fx2 * 10.0f + t * 1.2f);
          float v2 = sinf(fy * 8.0f + t * 0.9f);
          float v3 = sinf((fx2 + fy) * 6.0f + t * 1.5f);
          float v4 = sinf(sqrtf((fx2 - 0.5f) * (fx2 - 0.5f) + (fy - 0.5f) * (fy - 0.5f)) * 12.0f - t * 2.0f);
          float pval = (v1 + v2 + v3 + v4) * 0.25f * 0.5f + 0.5f;
          int rr = (int)(sinf(pval * 6.28f) * 127 * fade + 128 * fade);
          int gg = (int)(sinf(pval * 6.28f + 2.09f) * 127 * fade + 128 * fade);
          int bb = (int)(sinf(pval * 6.28f + 4.19f) * 127 * fade + 128 * fade);
          Uint32 pcol;
          SDL_Rect pr;
          if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
          if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
          if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
          pcol = SDL_MapRGBA(menu_surface->format, rr, gg, bb, SDL_ALPHA_OPAQUE);
          pr.x = px; pr.y = py; pr.w = 4; pr.h = 4;
          SDL_FillRect(menu_surface, &pr, pcol);
        }
      }
      SDL_UnlockSurface(menu_surface);
      break;
    }

    /* --- Effect 2: Wireframe "CGTERM" morphing into 3D shapes --- */
    case 2: {
      /*
       * 24 vertices define the letters C-G-T-E-R-M as 2D wireframe.
       * These morph into cube, diamond, star shapes.
       * Each shape has 24 verts + edges connecting them.
       */
      #define WF_NVERTS 24
      #define WF_NEDGES 30

      static const float shape_text[WF_NVERTS][3] = {
        /* C (0-3): top-left, top-right, bot-left, bot-right-open */
        {-5.0f,-1.0f, 0}, {-4.0f,-1.0f, 0}, {-5.0f, 1.0f, 0}, {-4.0f, 1.0f, 0},
        /* G (4-7): top-left, top-right, bot-left, mid-right */
        {-3.4f,-1.0f, 0}, {-2.4f,-1.0f, 0}, {-3.4f, 1.0f, 0}, {-2.4f, 1.0f, 0},
        /* T (8-11): top-left, top-right, mid-top, stem-bot */
        {-1.8f,-1.0f, 0}, {-0.8f,-1.0f, 0}, {-1.3f,-1.0f, 0}, {-1.3f, 1.0f, 0},
        /* E (12-15): top-left, top-right, bot-left, bot-right */
        {-0.2f,-1.0f, 0}, { 0.8f,-1.0f, 0}, {-0.2f, 1.0f, 0}, { 0.8f, 1.0f, 0},
        /* R (16-19): top-left, top-right, bot-left, knee */
        { 1.4f,-1.0f, 0}, { 2.4f,-1.0f, 0}, { 1.4f, 1.0f, 0}, { 2.4f, 1.0f, 0},
        /* M (20-23): bot-left, top-left, peak, top-right */
        { 3.0f, 1.0f, 0}, { 3.0f,-1.0f, 0}, { 3.75f, 0.0f, 0}, { 4.5f,-1.0f, 0}
      };

      static const float shape_cube[WF_NVERTS][3] = {
        /* 8 cube verts repeated 3x to fill 24 — scaled 2x */
        {-2,-2,-2},{2,-2,-2},{2,2,-2},{-2,2,-2},
        {-2,-2, 2},{2,-2, 2},{2,2, 2},{-2,2, 2},
        {-1,-1,-3},{1,-1,-3},{1,1,-3},{-1,1,-3},
        {-1,-1, 3},{1,-1, 3},{1,1, 3},{-1,1, 3},
        {-0.6f,-2.6f,0},{0.6f,-2.6f,0},{-0.6f,2.6f,0},{0.6f,2.6f,0},
        {0,-2,0},{0,2,0},{-2,0,0},{2,0,0}
      };

      static const float shape_sphere[WF_NVERTS][3] = {
        /* Evenly distributed points on a sphere — scaled 2x */
        { 0.0f, 3.0f, 0.0f}, { 0.0f,-3.0f, 0.0f},
        { 3.0f, 0.0f, 0.0f}, {-3.0f, 0.0f, 0.0f},
        { 0.0f, 0.0f, 3.0f}, { 0.0f, 0.0f,-3.0f},
        { 2.1f, 2.1f, 0.0f}, {-2.1f, 2.1f, 0.0f},
        { 2.1f,-2.1f, 0.0f}, {-2.1f,-2.1f, 0.0f},
        { 0.0f, 2.1f, 2.1f}, { 0.0f, 2.1f,-2.1f},
        { 0.0f,-2.1f, 2.1f}, { 0.0f,-2.1f,-2.1f},
        { 2.1f, 0.0f, 2.1f}, {-2.1f, 0.0f, 2.1f},
        { 2.1f, 0.0f,-2.1f}, {-2.1f, 0.0f,-2.1f},
        { 1.7f, 1.7f, 1.7f},{-1.7f, 1.7f, 1.7f},
        { 1.7f,-1.7f, 1.7f},{-1.7f,-1.7f, 1.7f},
        { 1.7f, 1.7f,-1.7f},{-1.7f,-1.7f,-1.7f}
      };

      static const float shape_star[WF_NVERTS][3] = {
        /* 6-pointed star — scaled 2x */
        { 0, 4.0f, 0}, { 0,-4.0f, 0}, { 4.0f, 0, 0}, {-4.0f, 0, 0},
        { 0, 0, 4.0f}, { 0, 0,-4.0f},
        { 2, 2, 0}, {-2, 2, 0}, { 2,-2, 0}, {-2,-2, 0},
        { 0, 2, 2}, { 0, 2,-2}, { 0,-2, 2}, { 0,-2,-2},
        { 2, 0, 2}, {-2, 0, 2}, { 2, 0,-2}, {-2, 0,-2},
        { 1.0f, 3.0f, 1.0f}, {-1.0f,-3.0f,-1.0f},
        { 3.0f, 1.0f,-1.0f}, {-3.0f,-1.0f, 1.0f},
        { 1.0f,-1.0f, 3.0f}, {-1.0f, 1.0f,-3.0f}
      };

      /* Edges — connect adjacent vertices */
      static const int wf_edges[WF_NEDGES][2] = {
        /* C */ {0,1},{0,2},{2,3},
        /* G */ {4,5},{4,6},{6,7},{5,7},
        /* T */ {8,9},{10,11},
        /* E */ {12,13},{12,14},{14,15},
        /* R */ {16,17},{16,18},{17,16+3},{18,19},
        /* M */ {20,21},{21,22},{22,23},{23,20+3+1},
        /* cross-links for 3D shapes */
        {0,4},{4,8},{8,12},{12,16},{16,20},
        {1,5},{3,7},{11,15},{19,23},{2,6}
      };

      /* 4 shapes to morph between: text -> cube -> sphere -> star -> text ... */
      #define WF_NSHAPES 4
      static const float (*wf_shapes[WF_NSHAPES])[3] = {
        shape_text, shape_cube, shape_sphere, shape_star
      };

      float morph_time = fmodf(t * 0.2f, (float)WF_NSHAPES);
      int shape_from = (int)morph_time % WF_NSHAPES;
      int shape_to = (shape_from + 1) % WF_NSHAPES;
      float mt = morph_time - (int)morph_time;
      /* Smooth easing */
      mt = mt * mt * (3.0f - 2.0f * mt);

      float wf_ca = cosf(t * 0.6f), wf_sa = sinf(t * 0.6f);
      float wf_cb = cosf(t * 0.4f), wf_sb = sinf(t * 0.4f);
      int scr_x[WF_NVERTS], scr_y[WF_NVERTS];

      for (i = 0; i < WF_NVERTS; i++) {
        float vx = wf_shapes[shape_from][i][0] + (wf_shapes[shape_to][i][0] - wf_shapes[shape_from][i][0]) * mt;
        float vy = wf_shapes[shape_from][i][1] + (wf_shapes[shape_to][i][1] - wf_shapes[shape_from][i][1]) * mt;
        float vz = wf_shapes[shape_from][i][2] + (wf_shapes[shape_to][i][2] - wf_shapes[shape_from][i][2]) * mt;
        float rx, rz, ry, rz2, scale;
        /* Rotate Y */
        rx = vx * wf_ca - vz * wf_sa; rz = vx * wf_sa + vz * wf_ca;
        /* Rotate X */
        ry = vy * wf_cb - rz * wf_sb; rz2 = vy * wf_sb + rz * wf_cb;
        scale = 200.0f / (rz2 + 6.0f);
        scr_x[i] = (int)(rx * scale) + ecx;
        scr_y[i] = (int)(ry * scale) + ecy;
      }

      /* Draw edges */
      for (i = 0; i < WF_NEDGES; i++) {
        int v0 = wf_edges[i][0], v1 = wf_edges[i][1];
        float hue = fmodf(t * 0.4f + (float)i * 0.05f, 1.0f);
        int rr, gg, bb;
        Uint32 ecol;
        float h6 = hue * 6.0f;
        float ff = h6 - (int)h6;
        switch ((int)h6 % 6) {
          case 0: rr=255; gg=(int)(255*ff);  bb=0;   break;
          case 1: rr=(int)(255*(1-ff)); gg=255; bb=0;   break;
          case 2: rr=0;   gg=255; bb=(int)(255*ff);  break;
          case 3: rr=0;   gg=(int)(255*(1-ff)); bb=255; break;
          case 4: rr=(int)(255*ff);  gg=0;   bb=255; break;
          default:rr=255; gg=0;   bb=(int)(255*(1-ff));  break;
        }
        rr = (int)(rr * fade); gg = (int)(gg * fade); bb = (int)(bb * fade);
        ecol = SDL_MapRGBA(menu_surface->format, rr, gg, bb, SDL_ALPHA_OPAQUE);
        if (scr_x[v0] >= 0 && scr_x[v0] < w && scr_y[v0] >= ey_top && scr_y[v0] < ey_bot &&
            scr_x[v1] >= 0 && scr_x[v1] < w && scr_y[v1] >= ey_top && scr_y[v1] < ey_bot)
          menu_draw_line(scr_x[v0], scr_y[v0], scr_x[v1], scr_y[v1], ecol);
      }

      /* Draw dots at vertices */
      SDL_LockSurface(menu_surface);
      for (i = 0; i < WF_NVERTS; i++) {
        int dx, dy;
        Uint32 dcol = SDL_MapRGBA(menu_surface->format,
          (int)(255 * fade), (int)(255 * fade), (int)(255 * fade), SDL_ALPHA_OPAQUE);
        for (dy = -2; dy <= 2; dy++)
          for (dx = -2; dx <= 2; dx++)
            if (dx*dx + dy*dy <= 4) {
              int ppx = scr_x[i] + dx, ppy = scr_y[i] + dy;
              if (ppx >= 0 && ppx < w && ppy >= ey_top && ppy < ey_bot)
                drawpixel(ppx, ppy, dcol);
            }
      }
      SDL_UnlockSurface(menu_surface);
      break;
    }

    /* --- Effect 3: Rotozoom with spinning "CGTERM" text --- */
    case 3: {
      int px, py;
      float rz_ca = cosf(t * 0.7f), rz_sa = sinf(t * 0.7f);
      float zoom = 1.5f + sinf(t * 0.4f) * 0.8f;

      SDL_LockSurface(menu_surface);
      for (py = ey_top; py < ey_bot; py += 2) {
        for (px = 0; px < w; px += 2) {
          float u = (float)(px - ecx) / zoom;
          float v = (float)(py - ecy) / zoom;
          float ru = u * rz_ca - v * rz_sa;
          float rv = u * rz_sa + v * rz_ca;
          int tu = ((int)(ru * 0.1f)) & 1;
          int tv = ((int)(rv * 0.1f)) & 1;
          int pat = tu ^ tv;
          int rr, gg, bb;
          Uint32 rcol;
          SDL_Rect rr2;
          if (pat) {
            float hue = fmodf(t * 0.2f + ru * 0.005f, 1.0f);
            if (hue < 0) hue += 1.0f;
            rr = (int)(sinf(hue * 6.28f) * 100 + 155);
            gg = (int)(sinf(hue * 6.28f + 2.09f) * 100 + 155);
            bb = (int)(sinf(hue * 6.28f + 4.19f) * 100 + 155);
          } else {
            rr = 15; gg = 10; bb = 30;
          }
          rr = (int)(rr * fade); gg = (int)(gg * fade); bb = (int)(bb * fade);
          if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
          if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
          if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
          rcol = SDL_MapRGBA(menu_surface->format, rr, gg, bb, SDL_ALPHA_OPAQUE);
          rr2.x = px; rr2.y = py; rr2.w = 2; rr2.h = 2;
          SDL_FillRect(menu_surface, &rr2, rcol);
        }
      }
      SDL_UnlockSurface(menu_surface);

      /* Zooming "CGTERM" — PXFX Disco, truly smooth zoom with wobble & outline */
      {
        const char *rz_text = "CGTERM";
        int rz_len = 6;
        /* Smooth zoom: continuous float scale 1.0..4.0 */
        float zoom_z = sinf(t * 0.5f);
        float fscale = 2.5f + zoom_z * 1.5f;  /* 1.0 to 4.0 */
        /* Vertical bob */
        float bob_y = sinf(t * 1.1f) * 12.0f;
        float char_fw = 24.0f * fscale;
        float total_fw = rz_len * char_fw;
        float tx_base = (float)ecx - total_fw * 0.5f;
        float ty_base = (float)ecy - 16.0f * fscale + bob_y;
        float bright = 0.3f + (zoom_z + 1.0f) * 0.35f;
        int ci;
        int outline = (int)(fscale + 0.5f);

        if (bright > 1.0f) bright = 1.0f;
        if (outline < 1) outline = 1;

        font_set_font(menu_font[3]);
        for (ci = 0; ci < rz_len; ci++) {
          /* Per-character wobble: each letter has its own sine offset */
          float wobble_x = sinf(t * 2.5f + ci * 1.2f) * 3.0f * fscale * 0.3f;
          float wobble_y = cosf(t * 2.0f + ci * 0.9f) * 4.0f * fscale * 0.3f;
          int cx2 = (int)(tx_base + ci * char_fw + wobble_x);
          int cy2 = (int)(ty_base + wobble_y);
          int ch_code = (unsigned char)rz_text[ci];
          int cr, cg2, cb2;

          /* Black outline: render at offsets */
          { int ox, oy;
            for (oy = -outline; oy <= outline; oy += outline) {
              for (ox = -outline; ox <= outline; ox += outline) {
                if (ox == 0 && oy == 0) continue;
                font_draw_char_color_fscale(ch_code, cx2 + ox, cy2 + oy,
                  0, 0, 0, fscale);
              }
            }
          }

          /* Foreground: white-cyan glow */
          cr = (int)(180 * bright * fade);
          cg2 = (int)(255 * bright * fade);
          cb2 = (int)(255 * bright * fade);
          if (cr > 255) cr = 255;
          if (cg2 > 255) cg2 = 255;
          if (cb2 > 255) cb2 = 255;
          font_draw_char_color_fscale(ch_code, cx2, cy2, cr, cg2, cb2, fscale);
        }
      }
      break;
    }

    } /* end switch */
    } /* end fx_pass loop */
  }

  /* Top black bar — covers effects that bleed into title area */
  r.x = 0; r.y = 0; r.w = w; r.h = 54;
  SDL_FillRect(menu_surface, &r, black);

  /* Top separator line */
  menu_draw_line(0, 54, w - 1, 54,
    SDL_MapRGBA(menu_surface->format, 0x00, 0xcc, 0xff, 0xff));

  /* "GENESIS PROJECT" — big plasma text using splash font */
  {
    const char *gp = "GENESIS PROJECT";
    int gplen = (int)strlen(gp);
    int gpx = w / 2 - gplen * 12;  /* 24px per char at 1x */
    char ch[2] = {0, 0};

    font_set_font(menu_font[2]);
    for (i = 0; i < gplen; i++) {
      int rr, gg, bb;
      int cy = 4 + (int)(sinf(t * 2.5f + i * 0.4f) * 4.0f);

      float px = (float)i * 0.8f + t * 2.0f;
      float py = t * 1.5f;
      float v1 = sinf(px * 0.5f);
      float v2 = sinf(py * 0.7f + px * 0.3f);
      float v3 = sinf((px + py) * 0.4f);
      float plasma = (v1 + v2 + v3) / 3.0f * 0.5f + 0.5f;

      rr = (int)(sinf(plasma * 6.28f) * 127 + 128);
      gg = (int)(sinf(plasma * 6.28f + 2.09f) * 127 + 128);
      bb = (int)(sinf(plasma * 6.28f + 4.19f) * 127 + 128);

      if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
      if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
      if (bb < 0) bb = 0; else if (bb > 255) bb = 255;

      ch[0] = gp[i];
      font_draw_string_color(gpx + i * 24, cy, ch, rr, gg, bb);
    }
  }

  /* "CGTerm 3.0 - SCENE EDiTiON" — centered below in light blue */
  {
    const char *title = "CGTERM 3.0 - ULTiMATE C64 BBS WAREZ SCENE EDiTiON";
    int tlen = (int)strlen(title);
    int tx = w / 2 - tlen * 5;
    int ty = 38;
    font_set_font(menu_font[0]);
    font_draw_string_color(tx, ty, title, 0x60, 0x80, 0xff);
  }

  /* Sine scroller — smooth sub-pixel scrolling */
  {
    static const char *scroll = "  "
      "GREETZ FLY OUT TO ALL ELiTE BBS SYSOPS, PETSCII ARTiSTS AND RETRO HACKERS WORLDWIDE! "
      "--- TRIAD - FAiRLiGHT - CENSOR - ONSLAUGHT - SHARKS - F4CG - ROLE - CAMELOT - EXCESS "
      "- BOOZE DESiGN - CREST - HOKUTO FORCE - LAXITY - NOSTALGIA - REMEMBER --- "
      "CALL SLiME CiTY BBS! THE OLDEST AND GOOZiEST BBS ON THE PLANET! "
      "CALL FROZEN FLOPPY BBS! YOUR FRiENDLY NEiGHBOURHOOD WAREZ BOARD! "
      "--- SUPPORT YOUR LOCAL BBS - KEEP THE SCENE ALiVE - SPREAD THE WAREZ ---    ";
    static int slen = 0;
    int nchars;
    float foffset;
    int ioffset;
    if (!slen) slen = (int)strlen(scroll);
    /* Smooth scroll: 2 pixels per frame */
    foffset = (float)(frame * 2);
    ioffset = (int)foffset % (slen * 10);
    nchars = w / 10 + 2;
    font_set_font(menu_font[1]);
    for (i = 0; i < nchars; i++) {
      int ci = (ioffset / 10 + i) % slen;
      int spx = i * 10 - (ioffset % 10);
      float wave = sinf(t * 3.0f + (float)spx * 0.02f);
      int spy = h - 80 + (int)(wave * 10.0f);
      char ch[2];
      ch[0] = scroll[ci];
      ch[1] = 0;
      if (spx >= 0 && spx < w && spy >= 40 && spy < h - 52)
        font_draw_string(spx, spy, ch);
    }
  }

  /* Bottom black bar — 50px tall for 3 lines */
  r.x = 0; r.y = h - 50; r.w = w; r.h = 50;
  SDL_FillRect(menu_surface, &r, black);

  /* Yellow separator line */
  menu_draw_line(0, h - 50, w - 1, h - 50,
    SDL_MapRGBA(menu_surface->format, 0x00, 0xcc, 0xff, 0xff));

  /* Credits — colorful */
  font_set_font(menu_font[0]);
  {
    int cx2 = w / 2;
    /* All three credit lines left-aligned to same position */
    int clx = cx2 - 130;
    /* "Modification by" in light blue, "m00p" in neon green */
    font_draw_string_color(clx, h - 48, "Modification by ", 0x60, 0x80, 0xff);
    font_draw_string_color(clx + 170, h - 48, "m00p", 0x00, 0xff, 0x66);
    /* "Music by" in light blue, "Mr.Death" in hot pink */
    font_draw_string_color(clx, h - 36, "Music by ", 0x60, 0x80, 0xff);
    font_draw_string_color(clx + 170, h - 36, "Mr.Death", 0xff, 0x40, 0x80);
    /* "Original code by" in light blue, "MagerValp" in cyan */
    font_draw_string_color(clx, h - 24, "Original code by ", 0x60, 0x80, 0xff);
    font_draw_string_color(clx + 170, h - 24, "MagerValp", 0x00, 0xee, 0xff);
  }

  /* ESC/X hint — keys in hot pink, actions in light blue */
  font_set_font(menu_font[0]);
  font_draw_string_color(8, h - 16, "ESC ", 0xff, 0x40, 0x80);
  font_draw_string_color(48, h - 16, "continue", 0x60, 0x80, 0xff);
  font_draw_string_color(w - 150, h - 16, "X ", 0xff, 0x40, 0x80);
  font_draw_string_color(w - 130, h - 16, "skip forever", 0x60, 0x80, 0xff);

  menu_dirty = SDL_TRUE;
}


/* Blocking disk format selector. Returns 0=D64, 1=D71, 2=D81, -1=cancelled */
int menu_select_disk_format(void) {
  SDL_Event ev;
  int selection = 0;
  const char *formats[] = {"D64 (170K - 35 tracks)", "D71 (340K - 70 tracks)", "D81 (800K - 80 tracks)"};

  for (;;) {
    int i;
    int cx = menu_width / 2;
    int cy = menu_height / 2;
    menu_cls();
    menu_draw_borderbox(cx - 200, cy - 65, cx + 200, cy + 65);

    font_set_font(menu_font[1]);
    font_draw_string_color(cx - 100, cy - 52, "Select disk format:", 0xff, 0x40, 0x80);

    for (i = 0; i < 3; i++) {
      if (i == selection) {
        SDL_Rect r;
        Uint32 sel = SDL_MapRGBA(menu_surface->format, 0x30, 0x50, 0xa0, 0xc0);
        r.x = cx - 185; r.y = cy - 26 + i * 22; r.w = 370; r.h = 20;
        SDL_FillRect(menu_surface, &r, sel);
      }
      font_set_font(i == selection ? menu_font[1] : menu_font[0]);
      if (i == selection)
        font_draw_string_color(cx - 180, cy - 24 + i * 22, formats[i], 0xff, 0xff, 0xff);
      else
        font_draw_string_color(cx - 180, cy - 24 + i * 22, formats[i], 0x60, 0x80, 0xff);
    }

    font_set_font(menu_font[0]);
    {
      int hx = cx - 180;
      int hy = cy + 48;
      font_draw_string_color(hx, hy, "Up/Down ", 0x00, 0xee, 0xff);
      font_draw_string_color(hx + 80, hy, "select  ", 0x00, 0xff, 0x66);
      font_draw_string_color(hx + 160, hy, "Enter ", 0x00, 0xee, 0xff);
      font_draw_string_color(hx + 210, hy, "OK  ", 0x00, 0xff, 0x66);
      font_draw_string_color(hx + 250, hy, "Esc ", 0x00, 0xee, 0xff);
      font_draw_string_color(hx + 290, hy, "cancel", 0x00, 0xff, 0x66);
    }

    menu_dirty = SDL_TRUE;
    menu_show();
    gfx_vbl();

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        switch (ev.key.keysym.sym) {
        case SDLK_UP: if (selection > 0) selection--; break;
        case SDLK_DOWN: if (selection < 2) selection++; break;
        case SDLK_SPACE:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          return selection;
        case SDLK_ESCAPE:
          return -1;
        default: break;
        }
      }
    }
    SDL_Delay(20);
  }
}


const char *menu_get_splash_font_name(int idx) {
  if (idx < 0 || idx >= NUM_SPLASH_FONTS) return "Unknown";
  return splash_font_names[idx];
}


/* Blocking splash font selector. Returns font index or -1 if cancelled. */
/* Generic scrolling font selector.
 * Returns selected index or -1 if cancelled. */
static int font_selector(const char *title,
                          const char **font_files, const char **font_names,
                          int num_fonts, int current,
                          int cell_w, int cell_h, int grid_w, int grid_h) {
  SDL_Event ev;
  int selection = current;
  char fname[1024];
  int max_visible = (menu_height - 80) / 20;  /* how many fit on screen */
  int scroll_top = 0;

  if (selection < 0 || selection >= num_fonts) selection = 0;

  for (;;) {
    int i;
    int lx = 20, rx = menu_width - 20;
    int ty = 30;
    Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);

    /* Keep selection visible */
    if (selection < scroll_top) scroll_top = selection;
    if (selection >= scroll_top + max_visible) scroll_top = selection - max_visible + 1;

    SDL_FillRect(menu_surface, NULL, solidbg);
    menu_draw_borderbox(10, 10, menu_width - 11, menu_height - 11);

    /* Title */
    font_set_font(menu_font[0]);
    font_draw_string_color(lx, 16, title, 0xff, 0x40, 0x80);

    /* Scroll indicator */
    if (scroll_top > 0) {
      font_draw_string_color(rx - 20, ty - 2, "^", 0x00, 0xee, 0xff);
    }

    /* Draw visible fonts */
    for (i = scroll_top; i < num_fonts && i < scroll_top + max_visible; i++) {
      int iy = ty + (i - scroll_top) * 20;

      if (i == selection) {
        SDL_Rect r;
        Uint32 sel = SDL_MapRGBA(menu_surface->format, 0x30, 0x50, 0xa0, 0xc0);
        r.x = lx; r.y = iy; r.w = menu_width - 40; r.h = 18;
        SDL_FillRect(menu_surface, &r, sel);
      }

      /* Font name */
      font_set_font(menu_font[0]);
      if (i == selection)
        font_draw_string_color(lx + 5, iy + 3, font_names[i], 0xff, 0xff, 0xff);
      else
        font_draw_string_color(lx + 5, iy + 3, font_names[i], 0x60, 0x80, 0xff);

      /* Preview using that font */
      {
        Font *preview;
        path_build_asset(fname, sizeof(fname), font_files[i]);
        preview = font_load_font(fname, cell_w, cell_h, grid_w, grid_h);
        if (preview) {
          Font *saved = font_set_font(preview);
          if (i == selection)
            font_draw_string_color(lx + 160, iy + 3, "AaBbCc 123", 0x00, 0xee, 0xff);
          else
            font_draw_string_color(lx + 160, iy + 3, "AaBbCc 123", 0x40, 0x60, 0x90);
          font_set_font(saved);
          font_free(preview);
        }
      }
    }

    /* Scroll down indicator */
    if (scroll_top + max_visible < num_fonts) {
      font_set_font(menu_font[0]);
      font_draw_string_color(rx - 20, ty + max_visible * 20 - 2, "v", 0x00, 0xee, 0xff);
    }

    /* Hints at bottom */
    font_set_font(menu_font[0]);
    {
      int hx = lx;
      int hy = menu_height - 26;
      font_draw_string_color(hx, hy, "Up/Down ", 0x00, 0xee, 0xff);
      font_draw_string_color(hx + 80, hy, "select  ", 0x00, 0xff, 0x66);
      font_draw_string_color(hx + 160, hy, "Enter ", 0x00, 0xee, 0xff);
      font_draw_string_color(hx + 210, hy, "OK  ", 0x00, 0xff, 0x66);
      font_draw_string_color(hx + 250, hy, "Esc ", 0x00, 0xee, 0xff);
      font_draw_string_color(hx + 290, hy, "cancel", 0x00, 0xff, 0x66);
    }

    menu_dirty = SDL_TRUE;
    menu_show();
    gfx_vbl();

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        switch (ev.key.keysym.sym) {
        case SDLK_UP: if (selection > 0) selection--; break;
        case SDLK_DOWN: if (selection < num_fonts - 1) selection++; break;
        case SDLK_SPACE:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          return selection;
        case SDLK_ESCAPE:
          return -1;
        default: break;
        }
      }
    }
    SDL_Delay(20);
  }
}


int menu_select_splash_font(void) {
  int sel = font_selector("Select header font:",
    splash_font_files, splash_font_names, NUM_SPLASH_FONTS,
    cfg_splashfont, 24, 32, 16, 16);
  if (sel >= 0) {
    Font *newf;
    char fname[1024];
    path_build_asset(fname, sizeof(fname), splash_font_files[sel]);
    newf = font_load_font(fname, 24, 32, 16, 16);
    if (newf) {
      if (menu_font[2] != menu_font[0] && menu_font[2] != menu_font[1])
        font_free(menu_font[2]);
      menu_font[2] = newf;
      cfg_splashfont = sel;
    }
  }
  return sel;
}


int menu_select_menu_font(void) {
  int sel = font_selector("Select menu font:",
    menu_font_files, menu_font_names, NUM_MENU_FONTS,
    cfg_menufont, 10, 12, 16, 16);
  if (sel >= 0) {
    Font *newf;
    char fname[1024];
    path_build_asset(fname, sizeof(fname), menu_font_files[sel]);
    newf = font_load_font(fname, 10, 12, 16, 16);
    if (newf) {
      if (menu_font[0] != menu_font[1]) font_free(menu_font[0]);
      menu_font[0] = newf;
      menu_font[1] = newf;
      cfg_menufont = sel;
    }
  }
  return sel;
}


void menu_draw_message(const char *message) {
  int cx = menu_width / 2;
  int cy = menu_height / 2;
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);
  SDL_FillRect(menu_surface, NULL, solidbg);
  menu_draw_borderbox(15, cy - 15, menu_width - 16, cy + 15);
  font_set_font(menu_font[0]);
  font_draw_string(cx - (int)strlen(message) * 5, cy - 6, message);
}


/* Show a message that auto-dismisses after timeout_ms, or on keypress */
/* Keyboard layout selector and test mode */
/* Blocking bookmark editor — shows all 3 fields, Tab to switch, Enter to save.
 * Returns 1 if saved, 0 if cancelled. */
int menu_edit_bookmark(char *name, int namesz, char *host, int hostsz, char *port, int portsz, int *mode) {
  SDL_Event ev;
  int field = 0;  /* 0=name, 1=host, 2=port, 3=mode */
  int cursor[3];
  char *bufs[3];
  int maxlen[3];
  const char *labels[] = {"BBS Name:", "Hostname:", "Port:"};
  int done = 0, saved = 0;
  int bm_mode = mode ? *mode : 0;

  bufs[0] = name; bufs[1] = host; bufs[2] = port;
  maxlen[0] = namesz - 1; maxlen[1] = hostsz - 1; maxlen[2] = portsz - 1;
  cursor[0] = (int)strlen(name);
  cursor[1] = (int)strlen(host);
  cursor[2] = (int)strlen(port);

  while (!done) {
    int i;
    int cx = menu_width / 2;
    int cy = menu_height / 2;
    int bx = cx - 200, by = cy - 60;
    Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);

    SDL_FillRect(menu_surface, NULL, solidbg);
    menu_draw_borderbox(bx, by, cx + 200, cy + 70);

    font_set_font(menu_font[0]);
    font_draw_string_color(bx + 10, by + 8, "[ ", 0x00, 0xee, 0xff);
    font_draw_string_color(bx + 30, by + 8, "EDiT BOOKMARK", 0xff, 0x40, 0x80);
    font_draw_string_color(bx + 170, by + 8, " ]", 0x00, 0xee, 0xff);

    for (i = 0; i < 3; i++) {
      int fy = by + 30 + i * 24;
      SDL_Rect r;

      /* Label */
      font_draw_string_color(bx + 10, fy, labels[i], 0x00, 0xee, 0xff);

      /* Input field background */
      r.x = bx + 110; r.y = fy - 2; r.w = 280; r.h = 16;
      SDL_FillRect(menu_surface, &r,
        (i == field) ? SDL_MapRGBA(menu_surface->format, 0x20, 0x30, 0x60, SDL_ALPHA_OPAQUE)
                     : SDL_MapRGBA(menu_surface->format, 0x10, 0x15, 0x30, SDL_ALPHA_OPAQUE));

      /* Value */
      if (i == field) {
        font_draw_string_color(bx + 112, fy, bufs[i], 0xff, 0xff, 0xff);
        /* Cursor */
        {
          SDL_Rect cr;
          cr.x = bx + 112 + cursor[i] * 10; cr.y = fy; cr.w = 8; cr.h = 12;
          SDL_FillRect(menu_surface, &cr,
            SDL_MapRGBA(menu_surface->format, 0xc0, 0x80, 0xff, 0x80));
        }
      } else {
        font_draw_string_color(bx + 112, fy, bufs[i], 0x60, 0x80, 0xff);
      }
    }

    /* Mode field (field 3) */
    {
      int fy = by + 30 + 3 * 24;
      SDL_Rect r;
      font_draw_string_color(bx + 10, fy, "Mode:", 0x00, 0xee, 0xff);
      r.x = bx + 110; r.y = fy - 2; r.w = 280; r.h = 16;
      SDL_FillRect(menu_surface, &r,
        (field == 3) ? SDL_MapRGBA(menu_surface->format, 0x20, 0x30, 0x60, SDL_ALPHA_OPAQUE)
                     : SDL_MapRGBA(menu_surface->format, 0x10, 0x15, 0x30, SDL_ALPHA_OPAQUE));
      if (field == 3) {
        font_draw_string_color(bx + 112, fy, bm_mode ? "ANSI" : "PETSCII", 0xff, 0xff, 0xff);
      } else {
        font_draw_string_color(bx + 112, fy, bm_mode ? "ANSI" : "PETSCII", 0x60, 0x80, 0xff);
      }
    }

    /* Hints */
    font_draw_string_color(bx + 10, cy + 58, "Tab ", 0x00, 0xee, 0xff);
    font_draw_string_color(bx + 50, cy + 58, "next  ", 0x00, 0xff, 0x66);
    font_draw_string_color(bx + 110, cy + 58, "Enter ", 0x00, 0xee, 0xff);
    font_draw_string_color(bx + 170, cy + 58, "save  ", 0x00, 0xff, 0x66);
    font_draw_string_color(bx + 230, cy + 58, "Esc ", 0x00, 0xee, 0xff);
    font_draw_string_color(bx + 270, cy + 58, "cancel", 0x00, 0xff, 0x66);

    menu_dirty = SDL_TRUE;
    menu_show();
    gfx_vbl();

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        int len = (field < 3) ? (int)strlen(bufs[field]) : 0;
        switch (ev.key.keysym.sym) {
        case SDLK_ESCAPE:
          done = 1; saved = 0;
          break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          done = 1; saved = 1;
          break;
        case SDLK_TAB:
          field = (field + 1) % 4;
          break;
        case SDLK_SPACE:
          if (field == 3) {
            bm_mode ^= 1;
          }
          break;
        case SDLK_BACKSPACE:
          if (field < 3 && cursor[field] > 0) {
            memmove(bufs[field] + cursor[field] - 1,
                    bufs[field] + cursor[field],
                    len - cursor[field] + 1);
            cursor[field]--;
          }
          break;
        case SDLK_DELETE:
          if (field < 3 && cursor[field] < len) {
            memmove(bufs[field] + cursor[field],
                    bufs[field] + cursor[field] + 1,
                    len - cursor[field]);
          }
          break;
        case SDLK_LEFT:
          if (field < 3 && cursor[field] > 0) cursor[field]--;
          if (field == 3) bm_mode ^= 1;
          break;
        case SDLK_RIGHT:
          if (field < 3 && cursor[field] < len) cursor[field]++;
          if (field == 3) bm_mode ^= 1;
          break;
        case SDLK_HOME:
          if (field < 3) cursor[field] = 0;
          break;
        case SDLK_END:
          if (field < 3) cursor[field] = len;
          break;
        default:
          if (field == 3) {
            /* Space or any key toggles mode */
            break;
          }
          if (ev.key.keysym.unicode >= 32 && ev.key.keysym.unicode < 127) {
            if (len < maxlen[field]) {
              memmove(bufs[field] + cursor[field] + 1,
                      bufs[field] + cursor[field],
                      len - cursor[field] + 1);
              bufs[field][cursor[field]] = (char)ev.key.keysym.unicode;
              cursor[field]++;
            }
          }
          break;
        }
      }
    }
    SDL_Delay(20);
  }
  if (saved && mode) {
    *mode = bm_mode;
  }
  return saved;
}


/* Blocking speed selector for SEQ posting. Returns 0-4 or -1 if cancelled. */
int menu_select_post_speed(void) {
  SDL_Event ev;
  int selection = 0;
  const char *speeds[] = {"300 bps (safest)", "1200 bps", "2400 bps", "4800 bps", "9600 bps (fast)"};

  for (;;) {
    int i;
    int cx = menu_width / 2;
    int cy = menu_height / 2;
    menu_cls();
    menu_draw_borderbox(cx - 170, cy - 70, cx + 170, cy + 80);

    font_set_font(menu_font[0]);
    font_draw_string_color(cx - 100, cy - 52, "Select posting speed:", 0xff, 0x40, 0x80);

    for (i = 0; i < 5; i++) {
      int iy = cy - 26 + i * 20;
      if (i == selection) {
        SDL_Rect r;
        Uint32 sel = SDL_MapRGBA(menu_surface->format, 0x30, 0x50, 0xa0, 0xc0);
        r.x = cx - 155; r.y = iy; r.w = 310; r.h = 18;
        SDL_FillRect(menu_surface, &r, sel);
        font_draw_string_color(cx - 150, iy + 2, speeds[i], 0xff, 0xff, 0xff);
      } else {
        font_draw_string_color(cx - 150, iy + 2, speeds[i], 0x60, 0x80, 0xff);
      }
    }

    font_draw_string_color(cx - 150, cy + 62, "Enter ", 0x00, 0xee, 0xff);
    font_draw_string_color(cx - 90, cy + 62, "select  ", 0x00, 0xff, 0x66);
    font_draw_string_color(cx + 10, cy + 62, "Esc ", 0x00, 0xee, 0xff);
    font_draw_string_color(cx + 50, cy + 62, "cancel", 0x00, 0xff, 0x66);

    menu_dirty = SDL_TRUE;
    menu_show();
    gfx_vbl();

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        switch (ev.key.keysym.sym) {
        case SDLK_UP: if (selection > 0) selection--; break;
        case SDLK_DOWN: if (selection < 4) selection++; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          return selection;
        case SDLK_ESCAPE:
          return -1;
        default: break;
        }
      }
    }
    SDL_Delay(20);
  }
}


void menu_keyboard_test(void) {
  SDL_Event ev;
  int done = 0;
  int mode = 0;  /* 0=selector, 1=test */
  int selection = 0;
  char fname[1024];

  /* Find .kbd files */
  #define MAX_KBD_FILES 20
  char kbd_files[MAX_KBD_FILES][64];
  char kbd_names[MAX_KBD_FILES][64];
  int num_kbd = 0;

  /* Scan for C64 keyboard profiles (the ones with -c64 in the name) */
  {
    const char *profiles[] = {
      "default.kbd",
      NULL
    };
    const char *names[] = {
      "Default (Unicode)",
      NULL
    };
    int pi;
    for (pi = 0; profiles[pi] && num_kbd < MAX_KBD_FILES; pi++) {
      FILE *test;
      path_build_asset(fname, sizeof(fname), profiles[pi]);
      test = fopen(fname, "r");
      if (test) {
        fclose(test);
        strncpy(kbd_files[num_kbd], profiles[pi], 63);
        strncpy(kbd_names[num_kbd], names[pi], 63);
        num_kbd++;
      }
    }
  }

  /* Keyboard test state */
  char last_key_name[64] = "";
  int last_petscii[4] = {0, 0, 0, 0};  /* normal, shift, cbm, ctrl */
  char last_sdl_name[32] = "";
  int last_mod = 0;  /* which modifier was active: 0=none, 1=shift, 2=cbm, 3=ctrl */
  int last_actual = 0;  /* the actual PETSCII value that would be sent */
  int last_sym = 0;  /* SDL keycode number */

  while (!done) {
    int lx = 20;
    int ty = 20;
    Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);

    SDL_FillRect(menu_surface, NULL, solidbg);
    menu_draw_borderbox(10, 10, menu_width - 11, menu_height - 11);

    if (mode == 0) {
      /* Layout selector */
      int i;
      int max_visible = (menu_height - 100) / 18;
      int scroll = 0;
      if (selection >= max_visible) scroll = selection - max_visible + 1;

      font_set_font(menu_font[0]);
      font_draw_string_color(lx, ty, "[ ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 20, ty, "SELECT KEYBOARD LAYOUT", 0xff, 0x40, 0x80);
      font_draw_string_color(lx + 240, ty, " ]", 0x00, 0xee, 0xff);

      for (i = scroll; i < num_kbd && i < scroll + max_visible; i++) {
        int iy = 44 + (i - scroll) * 18;
        if (i == selection) {
          SDL_Rect r;
          Uint32 sel = SDL_MapRGBA(menu_surface->format, 0x30, 0x50, 0xa0, 0xc0);
          r.x = lx; r.y = iy; r.w = menu_width - 40; r.h = 16;
          SDL_FillRect(menu_surface, &r, sel);
          font_draw_string_color(lx + 5, iy + 2, kbd_names[i], 0xff, 0xff, 0xff);
          font_draw_string_color(lx + 250, iy + 2, kbd_files[i], 0x00, 0xee, 0xff);
        } else {
          font_draw_string_color(lx + 5, iy + 2, kbd_names[i], 0x60, 0x80, 0xff);
          font_draw_string_color(lx + 250, iy + 2, kbd_files[i], 0x40, 0x60, 0x90);
        }
      }

      font_draw_string_color(lx, menu_height - 40, "Enter ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 60, menu_height - 40, "select  ", 0x00, 0xff, 0x66);
      font_draw_string_color(lx + 140, menu_height - 40, "T ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 160, menu_height - 40, "test keys  ", 0x00, 0xff, 0x66);
      font_draw_string_color(lx + 270, menu_height - 40, "Esc ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 310, menu_height - 40, "back", 0x00, 0xff, 0x66);

    } else {
      /* Keyboard test mode */
      font_set_font(menu_font[0]);
      font_draw_string_color(lx, ty, "[ ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 20, ty, "KEYBOARD TEST MODE", 0xff, 0x40, 0x80);
      font_draw_string_color(lx + 210, ty, " ]", 0x00, 0xee, 0xff);

      font_draw_string_color(lx, ty + 24, "Press any key to see its C64 mapping.", 0x60, 0x80, 0xff);
      font_draw_string_color(lx, ty + 38, "Try with Shift, Ctrl, and C= (Alt).", 0x60, 0x80, 0xff);

      /* Current layout — show just the filename, not the full path */
      {
        const char *kbdname = cfg_keyboard ? cfg_keyboard : "(default)";
        const char *slash = strrchr(kbdname, '/');
        if (!slash) slash = strrchr(kbdname, '\\');
        if (slash) kbdname = slash + 1;
        font_draw_string_color(lx, ty + 62, "Layout:", 0x00, 0xee, 0xff);
        font_draw_string_color(lx + 80, ty + 62, kbdname, 0x00, 0xff, 0x66);
      }

      if (last_key_name[0]) {
        char buf[80];
        const char *pc_mod_names[] = {"(none)", "Shift", "Alt (C=)", "Ctrl"};

        /* --- PC side --- */
        font_draw_string_color(lx, ty + 80, "PC Key:", 0x00, 0xee, 0xff);
        snprintf(buf, sizeof(buf), "%s  (SDL: %d  Mod: %s)", last_sdl_name, last_sym, pc_mod_names[last_mod]);
        font_draw_string_color(lx + 80, ty + 80, buf, 0xff, 0xff, 0xff);

        snprintf(buf, sizeof(buf), "PETSCii: $%02X (%d)", last_actual, last_actual);
        font_draw_string_color(lx, ty + 96, buf, 0x00, 0xff, 0x66);

        /* Divider line */
        {
          Uint32 divcol = SDL_MapRGBA(menu_surface->format, 0x20, 0x40, 0x60, SDL_ALPHA_OPAQUE);
          menu_draw_line(lx, ty + 124, menu_width - lx, ty + 124, divcol);
        }

        /* All 4 C64 modifier mappings */
        font_draw_string_color(lx, ty + 132, "C64 KEY MAPPiNG FOR THiS KEY:", 0xff, 0x40, 0x80);
        { int mi;
          int row_colors[4][3] = {{0x60,0x80,0xff}, {0x60,0x80,0xff}, {0x60,0x80,0xff}, {0x60,0x80,0xff}};
          row_colors[last_mod][0] = 0x00;
          row_colors[last_mod][1] = 0xff;
          row_colors[last_mod][2] = 0x66;

          for (mi = 0; mi < 4; mi++) {
            int ry = ty + 150 + mi * 16;
            const char *labels[] = {"Normal:     ", "SHiFT:      ", "C= (Alt):   ", "CTRL:       "};
            if (last_petscii[mi] == 0) {
              snprintf(buf, sizeof(buf), "(unmapped)");
            } else {
              snprintf(buf, sizeof(buf), "$%02X (%3d)", last_petscii[mi], last_petscii[mi]);
            }
            /* Active row gets arrow indicator */
            if (mi == last_mod) {
              font_draw_string_color(lx, ry, ">>", 0xff, 0xff, 0xff);
            }
            font_draw_string_color(lx + 25, ry, labels[mi], 0x00, 0xee, 0xff);
            font_draw_string_color(lx + 155, ry, buf,
              row_colors[mi][0], row_colors[mi][1], row_colors[mi][2]);
          }
        }

        /* Show the actual C64 character large on the right */
        if (last_actual >= 32 && last_actual < 128) {
          char ch[2];
          ch[0] = (char)last_actual; ch[1] = 0;
          font_set_font(menu_font[4]);  /* C64 Pro font */
          font_draw_string_color_scaled(menu_width - 100, ty + 140, ch, 0x00, 0xee, 0xff, 4);
        }
      } else {
        font_draw_string_color(lx, ty + 100, "Press a key...", 0x60, 0x60, 0x80);
      }

      font_set_font(menu_font[0]);
      font_draw_string_color(lx, menu_height - 40, "Esc ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 40, menu_height - 40, "back to layout selector", 0x00, 0xff, 0x66);
    }

    menu_dirty = SDL_TRUE;
    menu_show();
    gfx_vbl();

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        if (mode == 0) {
          /* Selector mode */
          switch (ev.key.keysym.sym) {
          case SDLK_ESCAPE:
            done = 1;
            break;
          case SDLK_UP:
            if (selection > 0) selection--;
            break;
          case SDLK_DOWN:
            if (selection < num_kbd - 1) selection++;
            break;
          case SDLK_t:
            mode = 1;
            last_key_name[0] = 0;
            break;
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
            /* Load selected layout */
            path_build_asset(fname, sizeof(fname), kbd_files[selection]);
            if (kbd_reload(fname) == 0) {
              cfg_keyboard = kbd_files[selection];
              menu_draw_message_timed("Keyboard layout loaded!", 2000);
            } else {
              menu_draw_message_timed("Failed to load layout!", 2000);
            }
            break;
          default:
            break;
          }
        } else {
          /* Test mode */
          if (ev.key.keysym.sym == SDLK_ESCAPE) {
            mode = 0;
          } else if (ev.key.keysym.sym != SDLK_LSHIFT && ev.key.keysym.sym != SDLK_RSHIFT &&
                     ev.key.keysym.sym != SDLK_LCTRL && ev.key.keysym.sym != SDLK_RCTRL &&
                     ev.key.keysym.sym != SDLK_LALT && ev.key.keysym.sym != SDLK_RALT) {
            /* Look up the PETSCII values for this key */
            int sym = ev.key.keysym.sym;
            if (sym >= 0 && sym < SDLK_LAST) {
              extern unsigned char keytable[][5];
              int mod = ev.key.keysym.mod;
              last_petscii[0] = keytable[sym][0];
              last_petscii[1] = keytable[sym][1];
              last_petscii[2] = keytable[sym][2];
              last_petscii[3] = keytable[sym][3];

              /* Determine which modifier is active */
              if (mod & KMOD_CTRL) {
                last_mod = 3;
                last_actual = keytable[sym][3];
              } else if (mod & KMOD_ALT) {
                last_mod = 2;  /* Alt = C= key */
                last_actual = keytable[sym][2];
              } else if (mod & (KMOD_SHIFT | KMOD_CAPS)) {
                last_mod = 1;
                last_actual = keytable[sym][1];
              } else {
                last_mod = 0;
                last_actual = keytable[sym][0];
              }

              /* Unicode-first override: show what kbd_getkey() would actually send */
              if (last_mod < 2) {  /* no Ctrl/Alt modifier */
                unsigned int uc = ev.key.keysym.unicode;
                if (uc >= 'a' && uc <= 'z') last_actual = uc - 32;
                else if (uc >= 'A' && uc <= 'Z') last_actual = uc + 128;
                else if (uc >= 32 && uc < 127) last_actual = (unsigned char)uc;
                else if (uc == 0x00E5) last_actual = 0x5B;  /* å */
                else if (uc == 0x00C5) last_actual = 0xDB;  /* Å */
                else if (uc == 0x00F6) last_actual = 0x5C;  /* ö */
                else if (uc == 0x00D6) last_actual = 0xDC;  /* Ö */
                else if (uc == 0x00E4) last_actual = 0x5D;  /* ä */
                else if (uc == 0x00C4) last_actual = 0xDD;  /* Ä */
              }

              last_sym = sym;
              snprintf(last_sdl_name, sizeof(last_sdl_name), "%s",
                       SDL_GetKeyName(ev.key.keysym.sym));
              snprintf(last_key_name, sizeof(last_key_name), "%s", last_sdl_name);
            }
          }
        }
      }
    }
    SDL_Delay(20);
  }
}


void menu_draw_message_timed(const char *message, int timeout_ms) {
  SDL_Event ev;
  unsigned int deadline;

  menu_draw_message(message);
  menu_show();
  gfx_vbl();

  deadline = SDL_GetTicks() + timeout_ms;
  while (SDL_GetTicks() < deadline) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        menu_hide();
        return;
      }
    }
    SDL_Delay(20);
  }
  menu_hide();
  /* Flush stale keypresses so they don't leak into the terminal */
  {
    SDL_Event flush_ev;
    while (SDL_PollEvent(&flush_ev)) {
      if (flush_ev.type == SDL_QUIT) exit(0);
    }
  }
}


/* Blocking bookmark info viewer/editor.
 * Shows note text with option to edit. */
void menu_show_bookmark_info(const char *alias, const char *host, int port) {
  const char *note;
  SDL_Event ev;
  int editing = 0;
  char editbuf[1024];
  int editlen = 0;
  int done = 0;

  note = cfg_get_bookmark_note(host, port);

  while (!done) {
    int lx = 20, rx = menu_width - 20;
    int ty = 20;
    int y;
    const char *p;
    Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);

    SDL_FillRect(menu_surface, NULL, solidbg);
    menu_draw_borderbox(10, 10, menu_width - 11, menu_height - 11);

    /* Title */
    font_set_font(menu_font[1]);
    font_draw_string_color(lx, ty, alias, 0x00, 0xee, 0xff);
    ty += 16;

    /* Host:port */
    font_set_font(menu_font[0]);
    {
      char hostline[128];
      snprintf(hostline, sizeof(hostline), "%s:%d", host, port);
      font_draw_string_color(lx, ty, hostline, 0x60, 0x80, 0xff);
    }
    ty += 20;

    /* Divider */
    menu_draw_line(lx, ty, rx, ty,
      SDL_MapRGBA(menu_surface->format, 0x20, 0x40, 0x60, SDL_ALPHA_OPAQUE));
    ty += 8;

    if (editing) {
      /* Show edit buffer with cursor */
      font_set_font(menu_font[0]);
      font_draw_string_color(lx, ty, "Edit note (Enter=save, Esc=cancel):", 0xff, 0x40, 0x80);
      ty += 16;

      /* Render edit buffer line by line */
      {
        int ei = 0;
        y = ty;
        while (ei <= editlen && y < menu_height - 40) {
          char line[80];
          int li = 0;
          while (ei < editlen && editbuf[ei] != '\n' && li < 60) {
            line[li++] = editbuf[ei++];
          }
          line[li] = 0;
          if (ei < editlen && editbuf[ei] == '\n') ei++;
          font_draw_string_color(lx, y, line, 0x00, 0xff, 0x66);
          y += 14;
          if (ei >= editlen) break;
        }
        /* Cursor indicator */
        font_draw_string_color(lx, y, "_", 0xff, 0xff, 0xff);
      }
    } else {
      /* Display note */
      font_set_font(menu_font[0]);
      if (!note || !note[0]) {
        font_draw_string_color(lx, ty, "(No notes yet)", 0x60, 0x60, 0x80);
      } else {
        p = note;
        y = ty;
        while (*p && y < menu_height - 40) {
          char line[80];
          int li = 0;
          while (*p && *p != '\n' && li < 60) {
            line[li++] = *p++;
          }
          line[li] = 0;
          if (*p == '\n') p++;
          font_draw_string_color(lx, y, line, 0x00, 0xff, 0x66);
          y += 14;
        }
      }
    }

    /* Hints at bottom */
    font_set_font(menu_font[0]);
    if (!editing) {
      font_draw_string_color(lx, menu_height - 26, "E ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 20, menu_height - 26, "edit  ", 0x00, 0xff, 0x66);
      font_draw_string_color(lx + 80, menu_height - 26, "Esc ", 0x00, 0xee, 0xff);
      font_draw_string_color(lx + 120, menu_height - 26, "back", 0x00, 0xff, 0x66);
    }

    menu_dirty = SDL_TRUE;
    menu_show();
    gfx_vbl();

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        if (editing) {
          switch (ev.key.keysym.sym) {
          case SDLK_ESCAPE:
            editing = 0;
            break;
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
            /* Save note */
            editbuf[editlen] = 0;
            cfg_set_bookmark_note(host, port, editbuf);
            note = cfg_get_bookmark_note(host, port);
            editing = 0;
            break;
          case SDLK_BACKSPACE:
            if (editlen > 0) editlen--;
            editbuf[editlen] = 0;
            break;
          default:
            if (ev.key.keysym.unicode >= 32 && ev.key.keysym.unicode < 127) {
              if (editlen < (int)sizeof(editbuf) - 2) {
                editbuf[editlen++] = (char)ev.key.keysym.unicode;
                editbuf[editlen] = 0;
              }
            }
            break;
          }
        } else {
          switch (ev.key.keysym.sym) {
          case SDLK_ESCAPE:
          case SDLK_n:
          case SDLK_q:
            done = 1;
            break;
          case SDLK_e:
            /* Start editing */
            editing = 1;
            if (note && note[0]) {
              strncpy(editbuf, note, sizeof(editbuf) - 1);
              editbuf[sizeof(editbuf) - 1] = 0;
              editlen = (int)strlen(editbuf);
            } else {
              editbuf[0] = 0;
              editlen = 0;
            }
            break;
          default:
            break;
          }
        }
      }
    }
    SDL_Delay(20);
  }
}


void menu_print_bookmark(int slot, int col, char *text) {
  int y, x;
  char label[12];
  int cx = menu_width / 2;

  y = 32 + slot * 17;
  x = (col == 0) ? 15 : cx + 10;

  /* Slot label */
  if (col == 0) {
    snprintf(label, sizeof(label), "%d", slot);
  } else {
    snprintf(label, sizeof(label), "%d", slot + 20);
  }

  font_set_font(menu_font[1]);
  font_draw_string_color(x, y, label, 0x00, 0xff, 0x66);
  font_set_font(menu_font[0]);
  if (text && text[0]) {
    font_draw_string_color(x + 20, y, text, 0x00, 0xee, 0xff);
  } else {
    /* Empty slot */
    font_draw_string_color(x + 20, y, "Add your BBS here", 0x50, 0x40, 0x70);
  }
}


void menu_draw_bookmarks_sel(int selected) {
  int i;
  int cx = menu_width / 2;
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);

  SDL_FillRect(menu_surface, NULL, solidbg);
  menu_draw_borderbox(7, 7, menu_width - 8, menu_height - 8);

  /* Title */
  {
    const char *bm_title = "ELiTE BULLETiN BOARD SYSTEMS";
    font_set_font(menu_font[1]);
    font_draw_string_color(cx - (int)strlen(bm_title) * 5, 12, bm_title, 0xff, 0x40, 0x80);
  }

  /* Divider line */
  menu_draw_line(cx, 32, cx, menu_height - 32,
    SDL_MapRGBA(menu_surface->format, 0x20, 0x40, 0x60, SDL_ALPHA_OPAQUE));

  /* Left column: slots 0-19 */
  for (i = 0; i < 20; ++i) {
    /* Highlight selected entry */
    if (i == selected) {
      SDL_Rect r;
      r.x = 12; r.y = 32 + i * 17 - 1; r.w = cx - 14; r.h = 15;
      SDL_FillRect(menu_surface, &r, selectcolor);
    }
    menu_print_bookmark(i, 0,
      (i < cfg_numbookmarks) ? cfg_bookmark_alias[i] : NULL);
  }

  /* Right column: slots 20-39 */
  for (i = 0; i < 20; ++i) {
    if (i + 20 == selected) {
      SDL_Rect r;
      r.x = cx + 8; r.y = 32 + i * 17 - 1; r.w = cx - 14; r.h = 15;
      SDL_FillRect(menu_surface, &r, selectcolor);
    }
    menu_print_bookmark(i, 1,
      (i + 20 < cfg_numbookmarks) ? cfg_bookmark_alias[i + 20] : NULL);
  }

  /* Hint */
  font_set_font(menu_font[0]);
  {
    int hx = 15;
    int hy = menu_height - 20;
    font_set_font(menu_font[0]);
    font_draw_string_color(hx, hy, "Enter/#=", 0x00, 0xee, 0xff);
    font_set_font(menu_font[1]);
    font_draw_string_color(hx + 80, hy, "connect", 0x00, 0xff, 0x66);
    font_set_font(menu_font[0]);
    font_draw_string_color(hx + 155, hy, "A=", 0x00, 0xee, 0xff);
    font_set_font(menu_font[1]);
    font_draw_string_color(hx + 175, hy, "add", 0x00, 0xff, 0x66);
    font_set_font(menu_font[0]);
    font_draw_string_color(hx + 210, hy, "E=", 0x00, 0xee, 0xff);
    font_set_font(menu_font[1]);
    font_draw_string_color(hx + 230, hy, "edit", 0x00, 0xff, 0x66);
    font_set_font(menu_font[0]);
    font_draw_string_color(hx + 275, hy, "D=", 0x00, 0xee, 0xff);
    font_set_font(menu_font[1]);
    font_draw_string_color(hx + 295, hy, "del", 0x00, 0xff, 0x66);
    font_set_font(menu_font[0]);
    font_draw_string_color(hx + 335, hy, "N=", 0x00, 0xee, 0xff);
    font_set_font(menu_font[1]);
    font_draw_string_color(hx + 355, hy, "notes", 0x00, 0xff, 0x66);
  }

  menu_dirty = SDL_TRUE;
}

void menu_draw_bookmarks(void) {
  menu_draw_bookmarks_sel(-1);
}


void menu_fs_draw(const char *title) {
  /* Fill entire surface with solid dark background — no bleed-through */
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x0a, 0x0a, 0x1e, SDL_ALPHA_OPAQUE);
  SDL_FillRect(menu_surface, NULL, solidbg);
  menu_draw_borderbox(7, 7, menu_width - 8, menu_height - 8);
  font_set_font(menu_font[1]);
  font_draw_string(12, 12, title);
}


void menu_fs_draw_path(const char *path) {
  SDL_Rect r;
  char truncpath[60];
  int pathlen = (int)strlen(path);
  int maxchars = (menu_width - 24) / 10;

  /* Clear path area */
  r.x = 12; r.y = 24; r.w = menu_width - 24; r.h = 14;
  SDL_FillRect(menu_surface, &r, bgcolor);

  /* Truncate from the left if too long */
  if (pathlen > maxchars - 1) {
    snprintf(truncpath, sizeof(truncpath), "...%s", path + pathlen - maxchars + 4);
  } else {
    snprintf(truncpath, sizeof(truncpath), "%s", path);
  }
  /* Replace backslashes with forward slashes for display
   * (C64 font maps 0x5C to pound sign) */
  {
    char *bp;
    for (bp = truncpath; *bp; bp++) {
      if (*bp == '\\') *bp = '/';
    }
  }

  font_set_font(menu_font[0]);
  font_draw_string_color(12, 26, truncpath, 0xc0, 0x80, 0xff);

  /* Navigation hints — keys in cyan, actions in neon green */
  {
    int hx = 12;
    int hy = menu_height - 20;
    font_set_font(menu_font[0]);
    font_draw_string_color(hx, hy, "C ", 0x00, 0xee, 0xff);
    font_draw_string_color(hx + 20, hy, "mkdir ", 0x00, 0xff, 0x66);
    font_draw_string_color(hx + 70, hy, "R ", 0x00, 0xee, 0xff);
    font_draw_string_color(hx + 90, hy, "ren ", 0x00, 0xff, 0x66);
    font_draw_string_color(hx + 130, hy, "X ", 0x00, 0xee, 0xff);
    font_draw_string_color(hx + 150, hy, "del ", 0x00, 0xff, 0x66);
    font_draw_string_color(hx + 190, hy, "F2 ", 0x00, 0xee, 0xff);
    font_draw_string_color(hx + 220, hy, "save path ", 0x00, 0xff, 0x66);
    font_draw_string_color(hx + 320, hy, "Esc ", 0x00, 0xee, 0xff);
    font_draw_string_color(hx + 360, hy, "cancel", 0x00, 0xff, 0x66);
  }

  menu_dirty = 1;
}


void menu_fs_clear(void) {
  SDL_Rect r;

  r.w = menu_width - 24;
  r.h = menu_height - 60;
  r.x = 12;
  r.y = 40;
  SDL_FillRect(menu_surface, &r, bgcolor);
  menu_dirty = 1;
}


/* entrytype: 0=file, 1=dir, 2=disk image, 3=special (Use this folder, <- Back) */
void menu_fs_draw_blocks_free(const char *text) {
  int len = (int)strlen(text);
  font_set_font(menu_font[0]);
  font_draw_string_color(menu_width - len * 10 - 15, 14, text, 0x00, 0xff, 0x66);
}


void menu_fs_draw_line(int line, const char *text, int selected, int entrytype, unsigned int filesize) {
  SDL_Rect r;
  int x, y;
  int maxname = (menu_width - 100) / 10;  /* leave room for size on right */
  char namebuf[64];
  char sizebuf[12];

  y = 42 + line * 14;
  x = 12;

  /* Only highlight the cursor row, not tagged rows */
  r.w = menu_width - 24;
  r.h = 14;
  r.x = x;
  r.y = y;
  SDL_FillRect(menu_surface, &r, selected ? selectcolor : bgcolor);

  /* Draw tag star in bright green if tagged (text starts with '*') */
  if (text[0] == '*') {
    font_set_font(menu_font[0]);
    font_draw_string_color(x, y + 1, "*", 0x00, 0xff, 0x66);
    text++;
    x += 10;
  } else if (text[0] == ' ') {
    text++;
    x += 10;
  }

  if (maxname > 50) maxname = 50;

  /* Format size string for files */
  sizebuf[0] = 0;
  if (entrytype == 0 && filesize > 0) {
    if (filesize >= 1048576) {
      snprintf(sizebuf, sizeof(sizebuf), "%dM", (int)(filesize / 1048576));
    } else if (filesize >= 1024) {
      snprintf(sizebuf, sizeof(sizebuf), "%dK", (int)(filesize / 1024));
    } else {
      snprintf(sizebuf, sizeof(sizebuf), "%d", filesize);
    }
  } else if (entrytype == 2 && filesize > 0) {
    if (filesize >= 1024) {
      snprintf(sizebuf, sizeof(sizebuf), "%dK", (int)(filesize / 1024));
    } else {
      snprintf(sizebuf, sizeof(sizebuf), "%d", filesize);
    }
  }

  /* Format name with type prefix */
  switch (entrytype) {
  case 1: /* directory */
    snprintf(namebuf, sizeof(namebuf), "%s", text);
    break;
  case 2: /* disk image */
    snprintf(namebuf, sizeof(namebuf), "%s", text);
    break;
  default:
    snprintf(namebuf, sizeof(namebuf), "%s", text);
    break;
  }

  /* Draw name */
  switch (entrytype) {
  case 1:
    /* "DIR:" in cyan, name in neon green */
    font_set_font(menu_font[1]);
    font_draw_string_color(x, y + 1, "DIR:", 0x00, 0xee, 0xff);
    font_draw_string_color(x + 50, y + 1, text, 0x00, 0xff, 0x66);
    break;
  case 2:
    /* "IMG:" in cyan, name in orange */
    font_set_font(menu_font[1]);
    font_draw_string_color(x, y + 1, "IMG:", 0x00, 0xee, 0xff);
    font_draw_string_color(x + 50, y + 1, text, 0xff, 0x88, 0x00);
    break;
  case 3: {
    /* Special entries (Use this folder, <- Back) in white */
    font_set_font(menu_font[1]);
    font_draw_string(x, y + 1, namebuf);
    break;
  }
  default:
    /* Regular files in light blue */
    font_set_font(menu_font[0]);
    font_draw_string_color(x, y + 1, namebuf, 0x60, 0x80, 0xff);
    break;
  }

  /* Draw size on the right */
  if (sizebuf[0]) {
    int sw = (int)strlen(sizebuf) * 10;
    font_set_font(menu_font[0]);
    font_draw_string_color(menu_width - 16 - sw, y + 1, sizebuf, 0x40, 0x80, 0x90);
  }

  menu_dirty = 1;
}
