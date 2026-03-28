#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "font.h"
#include "config.h"
#include "paths.h"
#include "gfx.h"
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
static Font *menu_font[2];


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
  bgcolor = SDL_MapRGBA(menu_surface->format, 0x10, 0x10, 0x20, 0xf0);
  fgcolor = SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0x00, SDL_ALPHA_OPAQUE);
  shadecolor = SDL_MapRGBA(menu_surface->format, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
  hilitecolor = SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0xff, SDL_ALPHA_OPAQUE);
  inputbg = SDL_MapRGBA(menu_surface->format, 0x20, 0x20, 0x20, 0xe0);
  cursorcolor = SDL_MapRGB(menu_surface->format, 0x80, 0x80, 0x00);
  selectcolor = SDL_MapRGBA(menu_surface->format, 0xd0, 0xd0, 0x20, 0xc0);

  font_init(menu_surface);

  path_build_asset(fname, sizeof(fname), "10x12yellow.bmp");
  if ((menu_font[0] = font_load_font(fname, 10, 12, 32, 4)) == NULL) {
    printf("Couldn't load %s\n", fname);
    SDL_FreeSurface(menu_surface);
    return(1);
  }
  path_build_asset(fname, sizeof(fname), "10x12white.bmp");
  if ((menu_font[1] = font_load_font(fname, 10, 12, 32, 4)) == NULL) {
    printf("Couldn't load %s\n", fname);
    SDL_FreeSurface(menu_surface);
    font_free(menu_font[0]);
    return(1);
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
  SDL_FillRect(cursorsurface, NULL, SDL_MapRGB(cursorsurface->format, 0xff, 0xff, 0x00));
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
    /* Category header — draw centered in cyan/light blue */
    SDL_Color hdr_color = {0x40, 0xc0, 0xff, 255};
    SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &hdr_color, 1, 1);
    font_set_font(menu_font[1]);
    font_draw_string(x - 10, y, text);
    /* Restore white */
    {
      SDL_Color white = {255, 255, 255, 255};
      SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &white, 1, 1);
    }
  } else if (key[0] != 0) {
    font_set_font(menu_font[1]);
    font_draw_string(x - strlen(key) * 5, y, key);
    font_set_font(menu_font[0]);
    font_draw_string(x + 26, y, text);
  }
}


static void menu_draw_ascii_line(int x, int y, const char *text) {
  font_set_font(menu_font[1]);
  font_draw_string(x, y, text);
}

static void menu_draw_item(int x, int y, const char *key, const char *text) {
  char line[64];
  if (key[0]) {
    snprintf(line, sizeof(line), "[%s] %s", key, text);
  } else {
    snprintf(line, sizeof(line), "    %s", text);
  }
  font_set_font(menu_font[0]);
  font_draw_string(x, y, line);
}

static void menu_draw_section(int x, int y, const char *title) {
  SDL_Color cyan = {0x40, 0xc0, 0xff, 255};
  SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &cyan, 1, 1);
  font_set_font(menu_font[1]);
  font_draw_string(x, y, title);
  {
    SDL_Color white = {255, 255, 255, 255};
    SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &white, 1, 1);
  }
}

void menu_print_menu(struct menu *menu) {
  int cx = menu_width / 2;
  int lx = 15;          /* left column x */
  int rx = cx + 10;     /* right column x */
  int ty;               /* current y */
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x10, 0x10, 0x20, SDL_ALPHA_OPAQUE);
  Uint32 linecol = SDL_MapRGBA(menu_surface->format, 0x40, 0x40, 0x60, SDL_ALPHA_OPAQUE);

  (void)menu;  /* We draw our own layout */

  SDL_FillRect(menu_surface, NULL, solidbg);

  /* ASCII art logo */
  ty = 10;
  menu_draw_ascii_line(lx, ty,      " ______ _______ _______ _______ ______ _______");
  menu_draw_ascii_line(lx, ty + 12, "|      |     __|_     _|    ___|   __ \\   |   |");
  menu_draw_ascii_line(lx, ty + 24, "|   ---|    |  | |   | |    ___|      <       |");
  menu_draw_ascii_line(lx, ty + 36, "|______|_______| |___| |_______|___|__|__|_|__|");

  /* Subtitle */
  font_set_font(menu_font[0]);
  font_draw_string(lx + 60, ty + 54, "3.0 - SCENE EDiTiON");

  /* Horizontal divider */
  ty = ty + 72;
  menu_draw_line(lx, ty, menu_width - lx, ty, linecol);

  /* Vertical divider */
  menu_draw_line(cx, ty, cx, menu_height - 30, linecol);

  /* Top half — CONNECTION (left) and TRANSFERS & FiLES (right) */
  ty += 8;
  menu_draw_section(lx, ty, "CONNECTION");
  menu_draw_item(lx, ty + 16, "B", "Bookmarks");
  menu_draw_item(lx, ty + 30, "D", "Connect/Disconnect");
  menu_draw_item(lx, ty + 44, "R", "Reconnect");

  menu_draw_section(rx, ty, "TRANSFERS & FiLES");
  menu_draw_item(rx, ty + 16, "T", "Transfer file");
  menu_draw_item(rx, ty + 30, "I", "Upload path");
  menu_draw_item(rx, ty + 44, "J", "Download path");
  menu_draw_item(rx, ty + 58, "U", "Unjoin image");
  menu_draw_item(rx, ty + 72, "N", "New disk image");

  /* Horizontal divider — middle of screen */
  {
    int mid_y = menu_height / 2;
    menu_draw_line(lx, mid_y, menu_width - lx, mid_y, linecol);

    /* Bottom half — SCREEN & MACROS (left) and SETTINGS (right) */
    ty = mid_y + 8;
    menu_draw_section(lx, ty, "SCREEN & MACROS");
    menu_draw_item(lx, ty + 16, "L", "Load seq file");
    menu_draw_item(lx, ty + 30, "S", "Save screen");
    menu_draw_item(lx, ty + 44, "C", "Record macro");
    menu_draw_item(lx, ty + 58, "V", "Play macro");
    menu_draw_item(lx, ty + 72, "A", "Abort");

    menu_draw_section(rx, ty, "SETTINGS");
    menu_draw_item(rx, ty + 16, "E", "Local echo");
    menu_draw_item(rx, ty + 30, "F", "Fullscreen");

    menu_draw_item(rx, ty + 58, "Q", "Quit CGTerm");
  }

  /* Border */
  menu_draw_box(5, 5, menu_width - 6, menu_height - 6,
    SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0x00, SDL_ALPHA_OPAQUE));
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
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x10, 0x10, 0x20, SDL_ALPHA_OPAQUE);
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

void menu_update_xfer_progress(const char *message, int bytes, int total) {
  SDL_Rect r;
  char line[64];
  int size;
  int blocks;

  if (bytes < 0) bytes = 0;
  if (total < 0) total = 0;
  if (total > 0 && bytes > total) bytes = total;

  /* Clear the dynamic area (below the static header lines) */
  r.x = 8;
  r.y = 74;
  r.w = menu_width - 16;
  r.h = 78;
  SDL_FillRect(menu_surface, &r, bgcolor);

  font_set_font(menu_font[0]);

  /* [FiLENAME]: */
  snprintf(line, sizeof(line), "[filename]: %.20s", xfer_disp_filename);
  font_draw_string(10, 74, line);

  /* [block number]: */
  blocks = (bytes + 253) / 254;
  if (total > 0) {
    int total_blocks = (total + 253) / 254;
    snprintf(line, sizeof(line), "[block number]: %d / %d", blocks, total_blocks);
  } else {
    snprintf(line, sizeof(line), "[block number]: %d", blocks);
  }
  font_draw_string(10, 86, line);

  /* [status]: */
  if (total > 0) {
    snprintf(line, sizeof(line), "[status]: %d of %d", bytes, total);
  } else {
    snprintf(line, sizeof(line), "[status]: %d", bytes);
  }
  font_draw_string(10, 98, line);

  /* Progress bar */
  r.x = 10;
  r.y = 114;
  r.w = 300;
  r.h = 11;
  SDL_FillRect(menu_surface, &r, inputbg);

  if (total > 0) {
    size = (300 * bytes) / total;
  } else {
    size = bytes % 300;
  }
  if (size < 0) size = 0;
  if (size > 300) size = 300;

  if (size > 0) {
    r.w = size;
    SDL_FillRect(menu_surface, &r, fgcolor);
  }

  menu_dirty = SDL_TRUE;
}



void menu_update_xfer_block_progress(const char *status, const char *protocol, int current_blocks, int total_blocks) {
  SDL_Rect r;
  char line[64];
  int size = 0;

  if (!status) status = "Downloading:";
  if (!protocol) protocol = "Unknown";
  if (current_blocks < 0) current_blocks = 0;
  if (total_blocks < 0) total_blocks = 0;
  if (total_blocks > 0 && current_blocks > total_blocks) current_blocks = total_blocks;

  /* Clear the dynamic area */
  r.x = 8;
  r.y = 74;
  r.w = menu_width - 16;
  r.h = 78;
  SDL_FillRect(menu_surface, &r, bgcolor);

  font_set_font(menu_font[0]);

  /* [FiLENAME]: */
  snprintf(line, sizeof(line), "[filename]: %.20s", xfer_disp_filename);
  font_draw_string(10, 74, line);

  /* [block number]: */
  if (total_blocks > 0) {
    snprintf(line, sizeof(line), "[block number]: %d / %d", current_blocks, total_blocks);
  } else {
    snprintf(line, sizeof(line), "[block number]: %d", current_blocks);
  }
  font_draw_string(10, 86, line);

  /* [status]: */
  snprintf(line, sizeof(line), "[status]: %s", status);
  font_draw_string(10, 98, line);

  /* Progress bar */
  r.x = 10;
  r.y = 114;
  r.w = 300;
  r.h = 11;
  SDL_FillRect(menu_surface, &r, inputbg);

  if (total_blocks > 0) {
    size = (300 * current_blocks) / total_blocks;
  } else if (current_blocks > 0) {
    size = (current_blocks * 20) % 300;
  }
  if (size < 0) size = 0;
  if (size > 300) size = 300;

  if (size > 0) {
    r.w = size;
    SDL_FillRect(menu_surface, &r, fgcolor);
  }

  menu_dirty = SDL_TRUE;
}

void menu_draw_xfer_progress(const char *filename, int direction, int protocol) {
  char s[64];

  /* Store state for update calls */
  snprintf(xfer_disp_filename, sizeof(xfer_disp_filename), "%s", filename);
  snprintf(xfer_disp_direction, sizeof(xfer_disp_direction), "%s", dir[direction]);
  snprintf(xfer_disp_protocol, sizeof(xfer_disp_protocol), "%s", proto[protocol]);

  menu_cls();
  menu_draw_borderbox(7, 47, menu_width - 8, menu_height - 38);

  /* [protocol]: uploading/downloading */
  font_set_font(menu_font[1]);
  snprintf(s, sizeof(s), "[%s]: %sing", proto[protocol],
    direction == 1 ? "upload" : "download");
  font_draw_string(10, 52, s);
  font_set_font(menu_font[0]);
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
  Uint32 black = SDL_MapRGBA(menu_surface->format, 0, 0, 0, SDL_ALPHA_OPAQUE);

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

  /* Morphing 3D wireframe shape — cube morphs to diamond and back */
  {
    static const float cube[8][3] = {
      {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
      {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1}
    };
    static const float diamond[8][3] = {
      {0,-1.5f,0},{1,0,-1},{0,0,-1.5f},{-1,0,-1},
      {0,-1.5f,0},{1,0, 1},{0,0, 1.5f},{-1,0, 1}
    };
    static const float star[8][3] = {
      {0,-1.8f,0},{1.8f,0,0},{0,1.8f,0},{-1.8f,0,0},
      {0,0,-1.8f},{1,1,1},{0,0,1.8f},{-1,-1,-1}
    };
    static const int edges[12][2] = {
      {0,1},{1,2},{2,3},{3,0},
      {4,5},{5,6},{6,7},{7,4},
      {0,4},{1,5},{2,6},{3,7}
    };
    /* Extra cross-edges for more complex wireframe */
    static const int xedges[4][2] = {
      {0,6},{1,7},{2,4},{3,5}
    };

    float ca = cosf(t), sa = sinf(t);
    float cb = cosf(t * 0.7f), sb = sinf(t * 0.7f);
    float cc = cosf(t * 0.4f), sc = sinf(t * 0.4f);
    int px[8], py[8];

    /* Morph between shapes */
    float morph_cycle = fmodf(t * 0.15f, 3.0f);
    float morph_t, verts[8][3];
    const float (*shape_a)[3], (*shape_b)[3];

    if (morph_cycle < 1.0f) {
      shape_a = cube; shape_b = diamond; morph_t = morph_cycle;
    } else if (morph_cycle < 2.0f) {
      shape_a = diamond; shape_b = star; morph_t = morph_cycle - 1.0f;
    } else {
      shape_a = star; shape_b = cube; morph_t = morph_cycle - 2.0f;
    }
    /* Smooth easing */
    morph_t = morph_t * morph_t * (3.0f - 2.0f * morph_t);

    for (i = 0; i < 8; i++) {
      verts[i][0] = shape_a[i][0] + (shape_b[i][0] - shape_a[i][0]) * morph_t;
      verts[i][1] = shape_a[i][1] + (shape_b[i][1] - shape_a[i][1]) * morph_t;
      verts[i][2] = shape_a[i][2] + (shape_b[i][2] - shape_a[i][2]) * morph_t;
    }

    for (i = 0; i < 8; i++) {
      float x = verts[i][0], y = verts[i][1], z = verts[i][2];
      float x2, z2, y2, z3, x3, y3;
      float scale;
      /* Rotate X */
      x2 = x * ca - z * sa; z2 = x * sa + z * ca;
      /* Rotate Y */
      y2 = y * cb - z2 * sb; z3 = y * sb + z2 * cb;
      /* Rotate Z */
      x3 = x2 * cc - y2 * sc; y3 = x2 * sc + y2 * cc;

      scale = 320.0f / (z3 + 5.0f);
      px[i] = (int)(x3 * scale) + w / 2;
      py[i] = (int)(y3 * scale) + h / 2 - 20;
    }

    /* Draw edges with color cycling */
    for (i = 0; i < 12; i++) {
      float hue = fmodf(t * 0.5f + i * 0.08f, 1.0f);
      int rr, gg, bb;
      float h6 = hue * 6.0f;
      float ff = h6 - (int)h6;
      int qv = (int)(255 * (1.0f - ff));
      int tv = (int)(255 * ff);
      Uint32 ecol;
      switch ((int)h6 % 6) {
        case 0: rr=255; gg=tv;  bb=0;   break;
        case 1: rr=qv;  gg=255; bb=0;   break;
        case 2: rr=0;   gg=255; bb=tv;  break;
        case 3: rr=0;   gg=qv;  bb=255; break;
        case 4: rr=tv;  gg=0;   bb=255; break;
        default:rr=255; gg=0;   bb=qv;  break;
      }
      ecol = SDL_MapRGBA(menu_surface->format, rr, gg, bb, SDL_ALPHA_OPAQUE);
      menu_draw_line(px[edges[i][0]], py[edges[i][0]],
                     px[edges[i][1]], py[edges[i][1]], ecol);
    }
    /* Extra cross-edges with dimmer color */
    for (i = 0; i < 4; i++) {
      float hue = fmodf(t * 0.3f + i * 0.2f + 0.5f, 1.0f);
      int rr = (int)(sinf(hue * 6.28f) * 80 + 100);
      int gg = (int)(sinf(hue * 6.28f + 2.0f) * 80 + 100);
      int bb = (int)(sinf(hue * 6.28f + 4.0f) * 80 + 100);
      Uint32 xcol;
      if (rr < 0) rr = 0; if (gg < 0) gg = 0; if (bb < 0) bb = 0;
      xcol = SDL_MapRGBA(menu_surface->format, rr, gg, bb, SDL_ALPHA_OPAQUE);
      menu_draw_line(px[xedges[i][0]], py[xedges[i][0]],
                     px[xedges[i][1]], py[xedges[i][1]], xcol);
    }
  }

  /* "GENESIS PROJECT" — wavy rainbow text at top center */
  {
    const char *gp = "GENESIS PROJECT";
    int gplen = (int)strlen(gp);
    int gpx = w / 2 - gplen * 5;
    char ch[2] = {0, 0};
    SDL_Color saved_palette[2];

    /* Save the original palette colors */
    memcpy(&saved_palette[0], &menu_font[1]->surface->format->palette->colors[0], sizeof(SDL_Color));
    memcpy(&saved_palette[1], &menu_font[1]->surface->format->palette->colors[1], sizeof(SDL_Color));

    font_set_font(menu_font[1]);
    for (i = 0; i < gplen; i++) {
      float hue = fmodf(t * 1.2f + i * 0.12f, 1.0f);
      int rr, gg, bb;
      float h6 = hue * 6.0f;
      float ff = h6 - (int)h6;
      int qv = (int)(255 * (1.0f - ff));
      int tv = (int)(255 * ff);
      int cy = 10 + (int)(sinf(t * 2.5f + i * 0.4f) * 5.0f);
      SDL_Color rainbow;

      switch ((int)h6 % 6) {
        case 0: rr=255; gg=tv;  bb=0;   break;
        case 1: rr=qv;  gg=255; bb=0;   break;
        case 2: rr=0;   gg=255; bb=tv;  break;
        case 3: rr=0;   gg=qv;  bb=255; break;
        case 4: rr=tv;  gg=0;   bb=255; break;
        default:rr=255; gg=0;   bb=qv;  break;
      }

      /* Set palette entry 1 to rainbow color for this character */
      rainbow.r = rr; rainbow.g = gg; rainbow.b = bb;
      SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &rainbow, 1, 1);

      ch[0] = gp[i];
      font_draw_string(gpx + i * 10, cy, ch);
    }

    /* Restore original palette */
    SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &saved_palette[1], 1, 1);
  }

  /* "CGTerm 3.0 - SCENE EDiTiON" — centered below */
  {
    const char *title = "CGTerm 3.0 - SCENE EDiTiON";
    int tlen = (int)strlen(title);
    int tx = w / 2 - tlen * 5;
    int ty = 30;
    font_set_font(menu_font[1]);
    font_draw_string(tx, ty, title);
  }

  /* Sine scroller — smooth sub-pixel scrolling */
  {
    static const char *scroll = "  Greetz to: TRIAD - FAIRLIGHT - CENSOR - ONSLAUGHT - CHORUS "
                         "- SHARKS - F4CG - ROLE - CAMELOT - GENESIS PROJECT - EXCESS  "
                         "... and everyone else keeping the scene alive!   ";
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
    SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0x00, 0xff));

  /* Credits — two lines */
  font_set_font(menu_font[0]);
  {
    const char *line1 = "Modification by m00p";
    const char *line2 = "Original code by MagerValp";
    font_draw_string(w / 2 - (int)strlen(line1) * 5, h - 46, line1);
    font_draw_string(w / 2 - (int)strlen(line2) * 5, h - 34, line2);
  }

  /* ESC/X hint */
  font_set_font(menu_font[1]);
  font_draw_string(8, h - 16, "ESC");
  font_set_font(menu_font[0]);
  font_draw_string(42, h - 16, "continue");
  font_set_font(menu_font[1]);
  font_draw_string(w - 130, h - 16, "X");
  font_set_font(menu_font[0]);
  font_draw_string(w - 116, h - 16, "skip forever");

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
    menu_draw_borderbox(cx - 160, cy - 50, cx + 160, cy + 50);

    font_set_font(menu_font[1]);
    font_draw_string(cx - 100, cy - 44, "Select disk format:");

    for (i = 0; i < 3; i++) {
      if (i == selection) {
        SDL_Rect r;
        Uint32 sel = SDL_MapRGBA(menu_surface->format, 0x80, 0x80, 0x00, 0xc0);
        r.x = cx - 150; r.y = cy - 22 + i * 18; r.w = 300; r.h = 16;
        SDL_FillRect(menu_surface, &r, sel);
      }
      font_set_font(i == selection ? menu_font[1] : menu_font[0]);
      font_draw_string(cx - 145, cy - 20 + i * 18, formats[i]);
    }

    font_set_font(menu_font[0]);
    font_draw_string(cx - 145, cy + 38, "Up/Down  Enter=OK  Esc=cancel");

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


void menu_draw_message(const char *message) {
  int cx = menu_width / 2;
  int cy = menu_height / 2;
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x10, 0x10, 0x20, SDL_ALPHA_OPAQUE);
  SDL_FillRect(menu_surface, NULL, solidbg);
  menu_draw_borderbox(15, cy - 15, menu_width - 16, cy + 15);
  font_set_font(menu_font[0]);
  font_draw_string(cx - (int)strlen(message) * 5, cy - 6, message);
}


void menu_print_bookmark(int slot, int col, char *text) {
  int y, x;
  char label[4];
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
  font_draw_string(x, y, label);
  font_set_font(menu_font[0]);
  if (text && text[0]) {
    font_draw_string(x + 20, y, text);
  } else {
    /* Empty slot */
    SDL_Color dimcol = {0x50, 0x50, 0x60, 255};
    SDL_SetPalette(menu_font[0]->surface, SDL_LOGPAL, &dimcol, 1, 1);
    font_draw_string(x + 20, y, "Add your BBS here");
    {
      SDL_Color yellow = {0xff, 0xff, 0x00, 255};
      SDL_SetPalette(menu_font[0]->surface, SDL_LOGPAL, &yellow, 1, 1);
    }
  }
}


void menu_draw_bookmarks(void) {
  int i;
  int cx = menu_width / 2;
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x10, 0x10, 0x20, SDL_ALPHA_OPAQUE);

  SDL_FillRect(menu_surface, NULL, solidbg);
  menu_draw_borderbox(7, 7, menu_width - 8, menu_height - 8);

  /* Title */
  {
    const char *bm_title = "ELiTE BULLETiN BOARD SYSTEMS";
    font_set_font(menu_font[1]);
    font_draw_string(cx - (int)strlen(bm_title) * 5, 12, bm_title);
  }

  /* Divider line */
  menu_draw_line(cx, 32, cx, menu_height - 32,
    SDL_MapRGBA(menu_surface->format, 0x40, 0x40, 0x60, SDL_ALPHA_OPAQUE));

  /* Left column: slots 0-19 */
  for (i = 0; i < 20; ++i) {
    menu_print_bookmark(i, 0,
      (i < cfg_numbookmarks) ? cfg_bookmark_alias[i] : NULL);
  }

  /* Right column: slots 20-39 */
  for (i = 0; i < 20; ++i) {
    menu_print_bookmark(i, 1,
      (i + 20 < cfg_numbookmarks) ? cfg_bookmark_alias[i + 20] : NULL);
  }

  /* Hint */
  font_set_font(menu_font[0]);
  font_draw_string(15, menu_height - 20, "#=connect  A=add  +=save current  Esc");
}


void menu_fs_draw(const char *title) {
  SDL_Rect r;
  /* Fill entire surface with solid dark background — no bleed-through */
  Uint32 solidbg = SDL_MapRGBA(menu_surface->format, 0x10, 0x10, 0x20, SDL_ALPHA_OPAQUE);
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

  {
    SDL_Color pathcol = {0x80, 0xc0, 0xff, 255};
    SDL_SetPalette(menu_font[0]->surface, SDL_LOGPAL, &pathcol, 1, 1);
    font_set_font(menu_font[0]);
    font_draw_string(12, 26, truncpath);
    {
      SDL_Color yellow = {0xff, 0xff, 0x00, 255};
      SDL_SetPalette(menu_font[0]->surface, SDL_LOGPAL, &yellow, 1, 1);
    }
  }

  /* Navigation hints */
  font_set_font(menu_font[0]);
  font_draw_string(12, menu_height - 20, "Bksp=up  Space/Enter=open  Esc=cancel");

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


void menu_fs_draw_line(int line, const char *text, int selected, int isdir) {
  SDL_Rect r;
  int x, y;
  int maxchars = (menu_width - 24) / 10;
  char string[64];

  y = 42 + line * 14;
  x = 12;

  r.w = menu_width - 24;
  r.h = 14;
  r.x = x;
  r.y = y;
  SDL_FillRect(menu_surface, &r, selected ? selectcolor : bgcolor);

  snprintf(string, maxchars < 63 ? maxchars + 1 : 63, "%-*s", maxchars, text);

  if (isdir) {
    /* Directories in cyan */
    SDL_Color dircol = {0x40, 0xd0, 0xff, 255};
    SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &dircol, 1, 1);
    font_set_font(menu_font[1]);
    font_draw_string(x, y + 1, string);
    {
      SDL_Color white = {255, 255, 255, 255};
      SDL_SetPalette(menu_font[1]->surface, SDL_LOGPAL, &white, 1, 1);
    }
  } else {
    /* Files in yellow */
    font_set_font(menu_font[0]);
    font_draw_string(x, y + 1, string);
  }
  menu_dirty = 1;
}
