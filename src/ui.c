#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "config.h"
#include "keyboard.h"
#include "net.h"
#include "kernal.h"
#include "modem.h"
#include "xfer.h"
#include "macro.h"
#include "ui.h"
#include "ansi.h"


int input_maxlen = 256;
int input_width;
int input_offset;
int input_pos;
int input_len;
Focus input_focus;
void (*input_done_call)(char *);
char input_buffer[256];
char input_display_buffer[100];

void inputupdate(void) {
  strncpy(input_display_buffer, input_buffer + input_offset, input_width);
  input_display_buffer[input_width] = 0;
  menu_update_input(input_display_buffer, input_pos);
}


void ui_inputcall(int width, char *title, char *text, void (*donecall)(char *), Focus focus) {
  input_done_call = donecall;
  input_focus = focus;
  if (text) {
    strncpy(input_buffer, text, sizeof(input_buffer) - 1);
    input_buffer[sizeof(input_buffer) - 1] = 0;
  } else {
    input_buffer[0] = 0;
  }
  input_width = width;
  if (text) {
    input_pos = input_len = strlen(text);
  } else {
    input_pos = input_len = 0;
  }
  if (input_len >= input_width) {
    input_offset = input_len - input_width + 1;
    input_pos -= input_offset;
  } else {
    input_offset = 0;
  }
  menu_draw_input(title);
  inputupdate();
  menu_show();
  kbd_focus = FOCUS_INPUTCALL;
}


void ui_inputkey(SDL_keysym *keysym) {
  int i;
  char c;

  switch (keysym->sym) {

  default:
    if (keysym->unicode <= 126 && keysym->unicode >= 32) {
      c = keysym->unicode;
      if (input_len < input_maxlen) {
	++input_len;
	memmove(input_buffer + input_pos + input_offset + 1,
		input_buffer + input_pos + input_offset,
		strlen(input_buffer + input_pos + input_offset) + 1);
	input_buffer[input_pos + input_offset] = c;
	input_buffer[input_len] = 0;

	if (input_pos + input_offset < input_len) {
	  if (input_pos < input_width - 1) {
	    ++input_pos;
	  } else if (input_len > input_offset + input_pos) {
	    ++input_offset;
	  }
	}

	inputupdate();
      }
    }
    break;

  case SDLK_RIGHT:
    if (input_pos + input_offset < input_len) {
      if (input_pos < input_width - 1) {
	++input_pos;
      } else if (input_len > input_offset + input_pos) {
	++input_offset;
      }
    }
    inputupdate();
    break;

  case SDLK_LEFT:
    if (input_pos > 0) {
      --input_pos;
    } else if (input_offset > 0) {
      --input_offset;
    }
    inputupdate();
    break;

  case SDLK_HOME:
    if (keysym->mod & KMOD_SHIFT) {
      input_pos = input_offset = input_len = 0;
      input_buffer[0] = 0;
    } else {
      if (input_pos || input_offset) {
	input_pos = input_offset = 0;
      }
    }
    inputupdate();
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    menu_hide();
    if (input_len) {
      kbd_focus = input_focus;
      input_done_call(input_buffer);
    } else {
      kbd_focus = FOCUS_TERM; // maybe
    }
    break;

  case SDLK_ESCAPE:
    menu_hide();
    kbd_focus = FOCUS_TERM; // maybe
    break;

  case SDLK_BACKSPACE:
    if (input_pos || input_offset) {
      if (input_pos) {
	--input_pos;
      } else {
	--input_offset;
      }
      i = input_offset + input_pos;
      while (input_buffer[i]) {
	input_buffer[i] = input_buffer[i+1];
	++i;
      }
      --input_len;
      inputupdate();
    }
    break;

  case SDLK_DELETE:
    if (input_pos + input_offset < input_len) {
      i = input_offset + input_pos;
      while (input_buffer[i]) {
	input_buffer[i] = input_buffer[i+1];
	++i;
      }
      --input_len;
      inputupdate();
    }
    break;

  }
}


void ui_display_net_status(int code, char *message) {
  if (code == 1) {
    kbd_focus = FOCUS_TERM;
    menu_hide();
  } else {
    menu_draw_message(message);
    menu_show();
    kbd_focus = FOCUS_REQUESTER;
  }
}


void ui_connect(char *host) {
  char *port;

  if ((port = strchr(input_buffer, ' '))) {
    *port++ = 0;
    cfg_port = strtol(port, (char **)NULL, 10);
    if (cfg_port <= 0 || cfg_port > 65535) {
      ui_display_net_status(2, "Illegal port number");
      return;
    }
  } else {
    cfg_port = 23;
  }
  cfg_sethost(input_buffer);
  ffd2(147);  /* clear screen before connecting */
  gfx_vbl();
  modem_connect(cfg_host, cfg_port, &ui_display_net_status);
}


void ui_menukey(SDL_keysym *keysym) {
  /* Check unicode for ? (help) — works on any keyboard layout */
  if (keysym->unicode == '?') {
    menu_hide();
    kbd_focus = FOCUS_TERM;
    ui_metakey(keysym);
    return;
  }
  switch (keysym->sym) {

  case SDLK_ESCAPE:
    menu_hide();
    kbd_focus = FOCUS_TERM;
    break;

  /* Valid menu keys — hide menu and handle */
  case SDLK_a: case SDLK_b: case SDLK_c: case SDLK_d:
  case SDLK_e: case SDLK_f: case SDLK_g: case SDLK_h: case SDLK_i: case SDLK_j:
  case SDLK_k: case SDLK_l: case SDLK_m: case SDLK_n:
  case SDLK_p: case SDLK_q: case SDLK_r: case SDLK_s:
  case SDLK_t: case SDLK_u: case SDLK_v: case SDLK_w:
  case SDLK_z:
  case SDLK_LALT: case SDLK_RALT:
    menu_hide();
    kbd_focus = FOCUS_TERM;
    ui_metakey(keysym);
    break;

  default:
    /* Ignore unknown keys — keep menu visible */
    break;
  }
}


void ui_requestkey(SDL_keysym *keysym) {
  switch (keysym->sym) {

  case SDLK_RETURN:
  case SDLK_ESCAPE:
    menu_hide();
    kbd_focus = FOCUS_TERM;
    break;

  default:
    break;
  }
}


static char bm_input[4] = "";
static int bm_input_len = 0;
static int bm_cursor = 0;
static char bm_new_alias[64];
static char bm_new_host[256];
static int bm_edit_index = -1;  /* -1 = adding new, >= 0 = editing */

/* Rewrite all bookmarks to the bookmark file */
static void bm_rewrite_all(void) {
  FILE *cfg;
  char fname[256];
  int i;
#ifdef WINDOWS
  snprintf(fname, sizeof(fname), "cgterm-bookmarks.cfg");
#else
  snprintf(fname, sizeof(fname), "%s/.cgterm-bookmarks", cfg_homedir);
#endif
  if ((cfg = fopen(fname, "w")) != NULL) {
    for (i = 0; i < cfg_numbookmarks; i++) {
      if (cfg_bookmark_termmode[i] == 1) {
        fprintf(cfg, "bookmark = %s, %s, %d, ansi\n",
          cfg_bookmark_alias[i], cfg_bookmark_host[i], cfg_bookmark_port[i]);
      } else {
        fprintf(cfg, "bookmark = %s, %s, %d\n",
          cfg_bookmark_alias[i], cfg_bookmark_host[i], cfg_bookmark_port[i]);
      }
    }
    fclose(cfg);
  }
}

static void bm_delete(int index) {
  int i;
  if (index < 0 || index >= cfg_numbookmarks) return;
  /* Free the strings */
  if (cfg_bookmark_alias[index]) free(cfg_bookmark_alias[index]);
  if (cfg_bookmark_host[index]) free(cfg_bookmark_host[index]);
  /* Shift remaining entries down */
  for (i = index; i < cfg_numbookmarks - 1; i++) {
    cfg_bookmark_alias[i] = cfg_bookmark_alias[i + 1];
    cfg_bookmark_host[i] = cfg_bookmark_host[i + 1];
    cfg_bookmark_port[i] = cfg_bookmark_port[i + 1];
  }
  cfg_bookmark_alias[cfg_numbookmarks - 1] = NULL;
  cfg_bookmark_host[cfg_numbookmarks - 1] = NULL;
  cfg_bookmark_port[cfg_numbookmarks - 1] = 0;
  cfg_numbookmarks--;
  bm_rewrite_all();
}

void bm_add_port(char *portstr) {
  int port = atoi(portstr);
  if (port <= 0 || port > 65535) port = 6400;
  if (bm_edit_index >= 0 && bm_edit_index < cfg_numbookmarks) {
    /* Editing existing bookmark */
    if (cfg_bookmark_alias[bm_edit_index]) free(cfg_bookmark_alias[bm_edit_index]);
    if (cfg_bookmark_host[bm_edit_index]) free(cfg_bookmark_host[bm_edit_index]);
    addhost(bm_edit_index, bm_new_alias, bm_new_host, port);
    bm_rewrite_all();
    bm_edit_index = -1;
    menu_draw_message("Bookmark updated!");
  } else if (cfg_numbookmarks < 40) {
    addhost(cfg_numbookmarks, bm_new_alias, bm_new_host, port);
    ++cfg_numbookmarks;
    cfg_save_bookmark(bm_new_alias, bm_new_host, port);
    menu_draw_message("Bookmark added!");
  } else {
    menu_draw_message("Bookmark list full!");
  }
  menu_show();
  gfx_vbl();
  kbd_focus = FOCUS_REQUESTER;
}

void bm_add_host(char *host) {
  snprintf(bm_new_host, sizeof(bm_new_host), "%s", host);
  ui_inputcall(6, "Port:", "6400", &bm_add_port, FOCUS_REQUESTER);
}

void bm_add_alias(char *alias) {
  snprintf(bm_new_alias, sizeof(bm_new_alias), "%s", alias);
  ui_inputcall(30, "Hostname:", "", &bm_add_host, FOCUS_REQUESTER);
}

void ui_bookmarkkey(SDL_keysym *keysym) {
  int b;
  char _DebugMsg[256];
  int max_bm = cfg_numbookmarks > 0 ? cfg_numbookmarks : 1;

  switch (keysym->sym) {

  case SDLK_ESCAPE:
  case SDLK_q:
    bm_input_len = 0;
    bm_input[0] = 0;
    menu_hide();
    kbd_focus = FOCUS_TERM;
    break;

  case SDLK_UP:
    if (bm_cursor > 0) {
      bm_cursor--;
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_DOWN:
    if (bm_cursor < max_bm - 1) {
      bm_cursor++;
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_LEFT:
    if (bm_cursor >= 20) {
      bm_cursor -= 20;
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_RIGHT:
    if (bm_cursor + 20 < max_bm) {
      bm_cursor += 20;
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_BACKSPACE:
  case SDLK_DELETE:
    if (bm_input_len > 0) {
      bm_input[--bm_input_len] = 0;
    }
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (bm_input_len > 0) {
      b = atoi(bm_input);
    } else {
      b = bm_cursor;
    }
    bm_input_len = 0;
    bm_input[0] = 0;
    if (b >= 0 && b < cfg_numbookmarks) {
      menu_hide();
      kbd_focus = FOCUS_TERM;
      cfg_sethost(cfg_bookmark_host[b]);
      cfg_port = cfg_bookmark_port[b];
      snprintf(cfg_connect_name, 128, "%s", cfg_bookmark_alias[b]);
      cfg_termmode = cfg_bookmark_termmode[b];
      if (cfg_termmode == 1) {
        gfx_set_columns(80);
        ansi_init();
      } else {
        gfx_set_columns(40);
      }
      snprintf(_DebugMsg, sizeof(_DebugMsg), "Attempting to connect to: %s on port: %d\n", cfg_bookmark_host[b], cfg_bookmark_port[b]);
      cfg_debug(_DebugMsg);
      ffd2(147);  /* clear screen */
      gfx_vbl();
      modem_connect(cfg_bookmark_host[b], cfg_bookmark_port[b], &ui_display_net_status);
    } else {
      menu_hide();
      kbd_focus = FOCUS_TERM;
    }
    break;

  case SDLK_a:
    /* Add new bookmark — use multi-field editor */
    {
      char nm[64] = "", ht[256] = "", pt[8] = "6400";
      int bm_mode = 0;
      if (menu_edit_bookmark(nm, sizeof(nm), ht, sizeof(ht), pt, sizeof(pt), &bm_mode)) {
        int port = atoi(pt);
        if (port <= 0 || port > 65535) port = 6400;
        if (nm[0] && ht[0] && cfg_numbookmarks < 40) {
          addhost(cfg_numbookmarks, nm, ht, port);
          cfg_bookmark_termmode[cfg_numbookmarks] = bm_mode;
          ++cfg_numbookmarks;
          cfg_save_bookmark(nm, ht, port);
        }
      }
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_e:
    /* Edit highlighted bookmark — multi-field editor */
    if (bm_cursor >= 0 && bm_cursor < cfg_numbookmarks) {
      char nm[64], ht[256], pt[8];
      int bm_mode = cfg_bookmark_termmode[bm_cursor];
      snprintf(nm, sizeof(nm), "%s", cfg_bookmark_alias[bm_cursor]);
      snprintf(ht, sizeof(ht), "%s", cfg_bookmark_host[bm_cursor]);
      snprintf(pt, sizeof(pt), "%d", cfg_bookmark_port[bm_cursor]);
      if (menu_edit_bookmark(nm, sizeof(nm), ht, sizeof(ht), pt, sizeof(pt), &bm_mode)) {
        int port = atoi(pt);
        if (port <= 0 || port > 65535) port = 6400;
        if (cfg_bookmark_alias[bm_cursor]) free(cfg_bookmark_alias[bm_cursor]);
        if (cfg_bookmark_host[bm_cursor]) free(cfg_bookmark_host[bm_cursor]);
        addhost(bm_cursor, nm, ht, port);
        cfg_bookmark_termmode[bm_cursor] = bm_mode;
        bm_rewrite_all();
      }
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_n:
    /* Show notes for highlighted bookmark */
    if (bm_cursor >= 0 && bm_cursor < cfg_numbookmarks) {
      menu_show_bookmark_info(cfg_bookmark_alias[bm_cursor],
                              cfg_bookmark_host[bm_cursor],
                              cfg_bookmark_port[bm_cursor]);
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_d:
    /* Delete highlighted bookmark — with confirmation */
    if (bm_cursor >= 0 && bm_cursor < cfg_numbookmarks) {
      char confirm_msg[128];
      snprintf(confirm_msg, sizeof(confirm_msg), "Delete \"%s\"? (Y/N)",
        cfg_bookmark_alias[bm_cursor]);
      menu_draw_message(confirm_msg);
      menu_show();
      gfx_vbl();
      /* Wait for Y or N */
      {
        SDL_Event cev;
        int answered = 0;
        while (!answered) {
          while (SDL_PollEvent(&cev)) {
            if (cev.type == SDL_QUIT) exit(0);
            if (cev.type == SDL_KEYDOWN) {
              if (cev.key.keysym.sym == SDLK_y) {
                bm_delete(bm_cursor);
                if (bm_cursor >= cfg_numbookmarks && cfg_numbookmarks > 0)
                  bm_cursor = cfg_numbookmarks - 1;
                answered = 1;
              } else {
                answered = 1;  /* any other key = cancel */
              }
            }
          }
          SDL_Delay(20);
        }
      }
      menu_draw_bookmarks_sel(bm_cursor);
      menu_show();
    }
    break;

  case SDLK_PLUS:
  case SDLK_EQUALS:  /* + is shift+= on many keyboards */
    /* Add current connection as bookmark */
    if (cfg_host && net_connected()) {
      if (cfg_numbookmarks < 40) {
        addhost(cfg_numbookmarks, cfg_host, cfg_host, cfg_port);
        ++cfg_numbookmarks;
        cfg_save_bookmark(cfg_host, cfg_host, cfg_port);
        menu_draw_message("Current BBS added to bookmarks!");
      } else {
        menu_draw_message("Bookmark list full!");
      }
      menu_show();
      gfx_vbl();
      kbd_focus = FOCUS_REQUESTER;
    } else {
      menu_draw_message("Not connected to any BBS");
      menu_show();
      gfx_vbl();
      kbd_focus = FOCUS_REQUESTER;
    }
    break;

  default:
    /* 0-9: instant connect to bookmark 0-9 */
    if (keysym->unicode >= '0' && keysym->unicode <= '9') {
      b = keysym->unicode - '0';
      bm_cursor = b;
      if (b < cfg_numbookmarks) {
        menu_hide();
        kbd_focus = FOCUS_TERM;
        cfg_sethost(cfg_bookmark_host[b]);
        cfg_port = cfg_bookmark_port[b];
        ffd2(147);  /* clear screen */
      gfx_vbl();
      modem_connect(cfg_bookmark_host[b], cfg_bookmark_port[b], &ui_display_net_status);
      } else {
        /* No bookmark at this slot — just move cursor */
        menu_draw_bookmarks_sel(bm_cursor);
        menu_show();
      }
    }
    break;
  }
}


void ui_pageup(void) {
  if (gfx_offset > cfg_columns) {
    gfx_set_offset(gfx_offset - cfg_columns);
  }
}


void ui_pagedown(void) {
  if (gfx_offset < gfx_maxoffset) {
    gfx_set_offset(gfx_offset + cfg_columns);
  }
}


void ui_init(void) {
  kbd_add_focus(FOCUS_MENU, &ui_menukey);
  kbd_add_focus(FOCUS_INPUTCALL, &ui_inputkey);
  kbd_add_focus(FOCUS_REQUESTER, &ui_requestkey);
  kbd_add_focus(FOCUS_BOOKMARKS, &ui_bookmarkkey);
  ui_local_init();
}
