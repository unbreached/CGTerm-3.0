#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "font.h"
#include "config.h"
#include "paths.h"
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
  bgcolor = SDL_MapRGBA(menu_surface->format, 0x40, 0x40, 0x40, 0xc0);
  fgcolor = SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0x00, SDL_ALPHA_OPAQUE);
  shadecolor = SDL_MapRGBA(menu_surface->format, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
  hilitecolor = SDL_MapRGBA(menu_surface->format, 0xff, 0xff, 0xff, SDL_ALPHA_OPAQUE);
  inputbg = SDL_MapRGBA(menu_surface->format, 0x20, 0x20, 0x20, 0xe0);
  cursorcolor = SDL_MapRGB(menu_surface->format, 0x80, 0x80, 0x00);
  selectcolor = SDL_MapRGBA(menu_surface->format, 0xd0, 0xd0, 0x20, 0xc0);

  font_init(menu_surface);

#ifdef WINDOWS
  if ((menu_font[0] = font_load_font("10x12yellow.bmp", 10, 12, 32, 4)) == NULL) {
    printf("Couldn't load 10x12yellow.bmp\n");
    SDL_FreeSurface(menu_surface);
    return(1);
  }
  if ((menu_font[1] = font_load_font("10x12white.bmp", 10, 12, 32, 4)) == NULL) {
    printf("Couldn't load 10x12white.bmp\n");
    SDL_FreeSurface(menu_surface);
    font_free(menu_font[0]);
    return(1);
  }
#else
  snprintf(fname, sizeof(fname), "%s/10x12yellow.bmp", cfg_prefix);
  if ((menu_font[0] = font_load_font(fname, 10, 12, 32, 4)) == NULL) {
    printf("Couldn't load %s\n", fname);
    SDL_FreeSurface(menu_surface);
    return(1);
  }
  snprintf(fname, sizeof(fname), "%s/10x12white.bmp", cfg_prefix);
  if ((menu_font[1] = font_load_font(fname, 10, 12, 32, 4)) == NULL) {
    printf("Couldn't load %s\n", fname);
    SDL_FreeSurface(menu_surface);
    font_free(menu_font[0]);
    return(1);
  }
#endif

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

  y = menu_height / 2 - 90 + line * 12;
  x = menu_width / 2 - 135;
  font_set_font(menu_font[1]);
  font_draw_string(x - strlen(key) * 5, y, key);
  font_set_font(menu_font[0]);
  font_draw_string(x + 26, y, text);
}


void menu_print_menu(struct menu *menu) {
  menu_cls();
  menu_draw_borderbox(7, 7, menu_width - 8, menu_height - 8);
  while (menu->row) {
    menu_print_key(menu->row - 1, menu->key, menu->text);
    ++menu;
  }
}


void menu_update_input(const char *text, int cursorpos) {
  SDL_Rect r;

  r.w = menu_width - 18;
  r.h = 14;
  r.x = 9;
  r.y = 101;
  SDL_FillRect(menu_surface, &r, inputbg);
  font_set_font(menu_font[0]);
  font_draw_string(10, 102, text);
  menu_dirty = SDL_TRUE;
  r.x = cursorpos * 10 + 10;
  r.y = 102;
  SDL_BlitSurface(cursorsurface, NULL, menu_surface, &r);
}


void menu_draw_input(const char *title) {
  menu_cls();
  menu_draw_borderbox(7, 80, menu_width - 8, 119);
  //menu_draw_box(8, 100, menu_width - 10, 117, fgcolor);
  font_set_font(menu_font[0]);
  font_draw_string(8, 82, title);
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
  snprintf(line, sizeof(line), "[fIlename]: %.20s", xfer_disp_filename);
  font_draw_string(10, 74, line);

  /* [block sIze]: */
  blocks = (bytes + 253) / 254;
  if (total > 0) {
    int total_blocks = (total + 253) / 254;
    snprintf(line, sizeof(line), "[block sIze]: %d / %d", blocks, total_blocks);
  } else {
    snprintf(line, sizeof(line), "[block sIze]: %d", blocks);
  }
  font_draw_string(10, 86, line);

  /* [status]: */
  if (total > 0) {
    snprintf(line, sizeof(line), "[status]: %d / %d bytes", bytes, total);
  } else if (message && message[0]) {
    snprintf(line, sizeof(line), "[status]: %s (%d)", message, bytes);
  } else {
    snprintf(line, sizeof(line), "[status]: %d bytes", bytes);
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
  snprintf(line, sizeof(line), "[fIlename]: %.20s", xfer_disp_filename);
  font_draw_string(10, 74, line);

  /* [block sIze]: */
  if (total_blocks > 0) {
    snprintf(line, sizeof(line), "[block sIze]: %d / %d", current_blocks, total_blocks);
  } else {
    snprintf(line, sizeof(line), "[block sIze]: %d", current_blocks);
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

  /* [protocol]: uploadIng/downloadIng */
  font_set_font(menu_font[1]);
  snprintf(s, sizeof(s), "[%s]: %sIng", proto[protocol],
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


void menu_draw_message(const char *message) {
  menu_cls();
  menu_draw_borderbox(7, 87, menu_width - 8, menu_height - 88);
  font_set_font(menu_font[0]);
  font_draw_string(160 - strlen(message)*5, 100 - 6, message);
}


void menu_print_bookmark(int line, char *text) {
  int y;
  int x;
  char num[3];

  y = 34 + line * 12;
  x = 20;
  snprintf(num, sizeof(num), "%2d", line);
  font_set_font(menu_font[1]);
  font_draw_string(x, y, num + 1);
  font_set_font(menu_font[0]);
  font_draw_string(x + 20, y, text);
}


void menu_draw_bookmarks(void) {
  int i;

  menu_cls();
  menu_draw_borderbox(7, 7, menu_width - 8, menu_height - 8);
  for (i = 0; i < 10; ++i) {
    if (i < cfg_numbookmarks) {
      menu_print_bookmark(i, cfg_bookmark_alias[i]);
    } else {
      menu_print_bookmark(i, "");
    }
  }
  font_set_font(menu_font[1]);
  font_draw_string(115, 10, "Bookmarks");
  font_set_font(menu_font[0]);
  font_draw_string(50, 166, "Press a key to connect");
}


void menu_fs_draw(const char *title) {
  menu_cls();
  menu_draw_borderbox(7, 7, menu_width - 8, menu_height - 8);
  font_set_font(menu_font[1]);
  font_draw_string(10, 10, title);
}


void menu_fs_clear(void) {
  SDL_Rect r;

  r.w = 300;
  r.h = 180;
  r.x = 10;
  r.y = 10;
  SDL_FillRect(menu_surface, &r, bgcolor);
  menu_dirty = 1;
}


void menu_fs_draw_line(int line, const char *text, int selected, int font) {
  SDL_Rect r;
  int x, y;
  char string[31];

  y = 30 + line * 12;
  x = 10;

  r.w = 300;
  r.h = 12;
  r.x = x;
  r.y = y;
  SDL_FillRect(menu_surface, &r, selected ? selectcolor : bgcolor);

  snprintf(string, sizeof(string), "%-30s", text);
  font_set_font(menu_font[font]);
  font_draw_string(x, y, string);
  menu_dirty = 1;
}
