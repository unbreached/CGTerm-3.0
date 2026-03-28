#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "config.h"
#include "keyboard.h"
#include "net.h"
#include "kernal.h"
#include "xfer.h"
#include "dir.h"
#include "diskimage.h"
#include "fileselector.h"
#include "macro.h"
#include "ui.h"


struct menu termmenu[] = {
  {1,  "",  "-- CONNECTION --"},
  {2,  "B", "Open bookmarks"},
  {3,  "D", "Connect/disconnect"},
  {4,  "R", "Reconnect"},
  {5,  "",  ""},
  {6,  "",  "-- TRANSFERS & FiLES --"},
  {7,  "T", "Transfer file"},
  {8,  "I", "Set upload path / image"},
  {9,  "J", "Set download path / image"},
  {10, "U", "Unjoin disk image"},
  {11, "N", "New disk image (D64/D71/D81)"},
  {12, "",  ""},
  {13, "",  "-- SCREEN & MACROS --"},
  {14, "L", "Load seq file"},
  {15, "S", "Save screen to seq file"},
  {16, "P", "Screenshot (BMP)"},
  {17, "C", "Start/stop recording macro"},
  {18, "V", "Play macro"},
  {19, "A", "Abort load or macro"},
  {20, "",  ""},
  {21, "",  "-- SETTINGS --"},
  {22, "E", "Toggle local echo"},
  {23, "F", "Toggle fullscreen mode"},
  {24, "",  ""},
  {25, "Q", "Quit CGTerm"},
  {0, NULL, NULL}
};


typedef enum selmode {
  SEL_DIR,
  SEL_FILE,
  SEL_MULTIFILE
} SelectMode;
FileSelector *fsel;
void (*select_done_call)(FileSelector *);
Focus select_focus;
SelectMode select_mode;


void kbd_select_dir_from(void (*donecall)(FileSelector *), Focus focus, const char *title, const char *startpath) {
  select_done_call = donecall;
  select_focus = focus;
  select_mode = SEL_DIR;
  fsel = fs_new(title, startpath);
  if (fsel) {
    fs_draw(fsel);
    menu_show();
    kbd_focus = FOCUS_SELECTDIR;
  }
}


void kbd_select_dir(void (*donecall)(FileSelector *), Focus focus) {
  kbd_select_dir_from(donecall, focus, "Select path / image", cfg_xferdir);
}


void kbd_select_file(void (*donecall)(FileSelector *), Focus focus) {
  select_done_call = donecall;
  select_focus = focus;
  select_mode = SEL_FILE;
  fsel = fs_new("Select file", cfg_xferdir);
  if (fsel) {
    fs_draw(fsel);
    menu_show();
    kbd_focus = FOCUS_SELECTDIR;
  }
}


void kbd_select_files(void (*donecall)(FileSelector *), Focus focus) {
  select_done_call = donecall;
  select_focus = focus;
  select_mode = SEL_MULTIFILE;
  fsel = fs_new("Tag files (space=tag enter=send)", cfg_xferdir);
  if (fsel) {
    fs_draw(fsel);
    menu_show();
    kbd_focus = FOCUS_SELECTDIR;
  }
}


void ui_selectdirkey(SDL_keysym *keysym) {
  if (fsel == NULL || fsel->numentries == 0) {
    menu_hide();
    kbd_focus = FOCUS_TERM;
    return;
  }

  switch (keysym->sym) {

  case SDLK_HOME:
    fsel->current = 0;
    fsel->offset = 0;
    fs_draw(fsel);
    break;

  case SDLK_ESCAPE:
    menu_hide();
    kbd_focus = FOCUS_TERM;
    break;

  case SDLK_DELETE:
  case SDLK_BACKSPACE:
    cfg_change_dir(fsel->path, "..");
    fs_read_dir(fsel, fsel->path);
    fs_draw(fsel);
    break;

  case SDLK_SPACE:
    fsel->selectedfile = dir_find(fsel->dir, fsel->current + fsel->offset);
    if (select_mode == SEL_MULTIFILE) {
      if (fsel->selectedfile->type == T_DIR) {
	/* Enter directory. "." means go up */
	if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, ".") == 0) {
	  cfg_change_dir(fsel->path, "..");
	} else {
	  cfg_change_dir(fsel->path, fsel->selectedfile->name);
	}
	fs_read_dir(fsel, fsel->path);
	fs_draw(fsel);
      } else {
	/* Toggle tag on file */
	fs_toggle_tag(fsel, fsel->current + fsel->offset);
	fs_draw(fsel);
      }
    } else {
      /* SEL_DIR and SEL_FILE: space enters directories */
      if (fsel->selectedfile->type == T_DIR) {
	if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, ".") == 0) {
	  cfg_change_dir(fsel->path, "..");
	} else {
	  cfg_change_dir(fsel->path, fsel->selectedfile->name);
	}
	fs_read_dir(fsel, fsel->path);
	fs_draw(fsel);
      }
    }
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    fsel->selectedfile = dir_find(fsel->dir, fsel->current + fsel->offset);
    /* "." always goes back, even inside a D64/D81 */
    if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, ".") == 0) {
      cfg_change_dir(fsel->path, "..");
      fs_read_dir(fsel, fsel->path);
      fs_draw(fsel);
      break;
    }
    /* "[ Use this folder ]" entry — select current directory or image */
    if (fsel->selectedfile->name &&
        strcmp(fsel->selectedfile->name, "[ Use this folder ]") == 0) {
      menu_hide();
      kbd_focus = select_focus;
      select_done_call(fsel);
      fs_free(fsel);
    } else if (fsel->selectedfile->type == T_DIR) {
      /* Enter directory. "." means go up one level */
      if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, ".") == 0) {
        cfg_change_dir(fsel->path, "..");
      } else {
        cfg_change_dir(fsel->path, fsel->selectedfile->name);
      }
      fs_read_dir(fsel, fsel->path);
      fs_draw(fsel);
    } else if (select_mode == SEL_DIR) {
      menu_hide();
      kbd_focus = select_focus;
      select_done_call(fsel);
      fs_free(fsel);
    } else if (select_mode == SEL_MULTIFILE) {
      /* Send all tagged files */
      if (fsel->numtagged > 0) {
	menu_hide();
	kbd_focus = select_focus;
	select_done_call(fsel);
	fs_free(fsel);
      }
    } else {
      if (fsel->selectedfile->type == T_PRG || fsel->selectedfile->type == T_SEQ) {
	menu_hide();
	kbd_focus = select_focus;
	select_done_call(fsel);
	fs_free(fsel);
      }
    }
    break;

  case SDLK_DOWN:
    if (fsel->current + fsel->offset + 1 < fsel->numentries) {
      if (fsel->current < fsel->filesperpage - 1) {
	fs_draw_name(fsel, fsel->current + fsel->offset, fsel->current, 0);
	fsel->current += 1;
	fs_draw_name(fsel, fsel->current + fsel->offset, fsel->current, 1);
      } else {
	fsel->current = 0;
	fsel->offset += fsel->filesperpage;
	fs_draw(fsel);
      }
    }
    break;

  case SDLK_UP:
    if (fsel->current + fsel->offset > 0) {
      if (fsel->current == 0) {
	fsel->current = fsel->filesperpage - 1;
	fsel->offset -= fsel->filesperpage;
	fs_draw(fsel);
      } else {
	fs_draw_name(fsel, fsel->current + fsel->offset, fsel->current, 0);
	fsel->current -= 1;
	fs_draw_name(fsel, fsel->current + fsel->offset, fsel->current, 1);
      }
    }
    break;

  default:
    break;
  }
}


void select_set_xferdir(FileSelector *fs) {
  snprintf(cfg_xferdir, 256, "%s", fs->path);
}

void select_set_dldir(FileSelector *fs) {
  snprintf(cfg_dldir, 256, "%s", fs->path);
}


/* New disk image creation */
static char new_d64_path[256];
static int new_d64_size = 174848;  /* default D64 */
static const char *new_d64_ext = ".d64";

void ui_create_d64_label(char *label) {
  DiskImage *di;
  unsigned char rawname[16];
  unsigned char rawid[2] = { '0', '0' };
  char msg[256];

  di = di_create_image(new_d64_path, new_d64_size);
  if (di == NULL) {
    menu_draw_message("Couldn't create disk image!");
    menu_show();
    gfx_vbl();
    return;
  }

  di_rawname_from_name(rawname, label);
  di_format(di, rawname, rawid);
  di_free_image(di);

  /* Set download directory to the new .d64 */
  snprintf(cfg_dldir, 256, "%s", new_d64_path);

  snprintf(msg, sizeof(msg), "Created: %s", new_d64_path);
  menu_draw_message(msg);
  menu_show();
  gfx_vbl();
}

static char new_d64_dir[256];

static char new_d64_filename[64];

void select_d64_dir_done(FileSelector *fs) {
  /* Final step: got the directory, now create the image */
  snprintf(new_d64_dir, sizeof(new_d64_dir), "%s", fs->path);
  /* Build full path */
  snprintf(new_d64_path, sizeof(new_d64_path), "%s%c%s",
    new_d64_dir,
#ifdef WINDOWS
    '\\',
#else
    '/',
#endif
    new_d64_filename);
  ui_inputcall(16, "Disk label:", "c64warez", &ui_create_d64_label, FOCUS_REQUESTER);
}

void ui_create_d64_name_done(char *filename) {
  /* Step 2 done: got filename, now pick directory */
  snprintf(new_d64_filename, sizeof(new_d64_filename), "%s", filename);
  /* Add extension if missing */
  if (strchr(new_d64_filename, '.') == NULL) {
    size_t len = strlen(new_d64_filename);
    snprintf(new_d64_filename + len, sizeof(new_d64_filename) - len, "%s", new_d64_ext);
  }
  kbd_select_dir_from(&select_d64_dir_done, FOCUS_TERM, "Save where?", cfg_dldir);
}

void ui_select_disk_format(void) {
  /* Step 1: pick format, then filename, then directory */
  int selection = menu_select_disk_format();
  const int sizes[] = {174848, 349696, 819200};
  const char *exts[] = {".d64", ".d71", ".d81"};
  char defname[32];

  if (selection < 0) {
    menu_hide();
    kbd_focus = FOCUS_TERM;
    return;
  }

  new_d64_size = sizes[selection];
  new_d64_ext = exts[selection];

  snprintf(defname, sizeof(defname), "c64warez%s", new_d64_ext);
  ui_inputcall(20, "Image filename:", defname, &ui_create_d64_name_done, FOCUS_REQUESTER);
}


void select_send_file(FileSelector *fs) {
  xfer_send(fs->selectedfile->name);
  /* Force full redraw of completion message */
  menu_cls();
  menu_draw_message("Transfer complete. Press any key.");
  menu_show();
  gfx_vbl();
  gfx_vbl();
}


void select_send_multipunter(FileSelector *fs) {
  xfer_send_multipunter(fs);
}


void ui_metakey(SDL_keysym *keysym) {
  switch (keysym->sym) {

  case SDLK_LALT:
  case SDLK_RALT:
    gfx_toggle_font();
    break;

  case SDLK_a:
    kbd_loadseq_abort();
    if (macro_play) {
      macro_play = 0;
    }
    break;

  case SDLK_b:
    if (net_connected()) {
      menu_draw_message("Disconnect first.");
      menu_show();
      kbd_focus = FOCUS_REQUESTER;
    } else {
      menu_draw_bookmarks_sel(0);
      menu_show();
      kbd_focus = FOCUS_BOOKMARKS;
    }
    break;
    
  case SDLK_c:
    if (macro_rec) {
      macro_rec = 0;
    } else {
      macro_len = 0;
      macro_rec = 1;
    }
    break;

  case SDLK_d:
    if (net_connected()) {
      net_disconnect();
    } else {
      ui_inputcall(30, "Connect to host [port]:", cfg_host, &ui_connect, FOCUS_TERM);
    }
    break;

  case SDLK_e:
    cfg_localecho ^= 1;
    break;

  case SDLK_f:
    gfx_toggle_fullscreen();
    break;

  case SDLK_i:
    kbd_select_dir(&select_set_xferdir, FOCUS_TERM);
    break;

  case SDLK_j:
    kbd_select_dir_from(&select_set_dldir, FOCUS_TERM, "Set download path / image", cfg_dldir);
    break;

  case SDLK_l:
    ui_inputcall(30, "Load SEQ file:", "screen.seq", &kbd_loadseq, FOCUS_TERM);
    break;

  case SDLK_p:
    {
      time_t now;
      struct tm *tm_info;
      char bmpname[64];
      char bmppath[512];
      char msg[256];

      now = time(NULL);
      tm_info = localtime(&now);
      strftime(bmpname, sizeof(bmpname), "cgterm_%Y%m%d_%H%M%S.bmp", tm_info);
#ifdef WINDOWS
      snprintf(bmppath, sizeof(bmppath), "%s\\%s", cfg_dldir, bmpname);
#else
      snprintf(bmppath, sizeof(bmppath), "%s/%s", cfg_dldir, bmpname);
#endif
      if (gfx_save_screenshot(bmppath) == 0) {
        snprintf(msg, sizeof(msg), "Screenshot saved: %s", bmpname);
      } else {
        snprintf(msg, sizeof(msg), "Screenshot failed!");
      }
      menu_draw_message(msg);
      menu_show();
      kbd_focus = FOCUS_REQUESTER;
    }
    break;

  case SDLK_n:
    /* Format → filename → directory → label → create */
    ui_select_disk_format();
    break;

  case SDLK_u:
    /* Unjoin disk image — reset download path to ~/Downloads */
    {
      char *home = cfg_homedir;
      if (home && home[0] != '.') {
#ifdef WINDOWS
        snprintf(cfg_dldir, 256, "%s\\Downloads", home);
#else
        snprintf(cfg_dldir, 256, "%s/Downloads", home);
#endif
      }
      menu_draw_message("Disk image unjoined");
      menu_show();
      kbd_focus = FOCUS_REQUESTER;
    }
    break;

  case SDLK_q:
    exit(0);
    break;

  case SDLK_r:
    if (!net_connected()) {
      if (cfg_host) {
	net_connect(cfg_host, cfg_port, &ui_display_net_status);
      }
    }
    break;

  case SDLK_s:
    ui_inputcall(30, "Save SEQ file:", "screen.seq", &gfx_savescreen, FOCUS_TERM);
    break;

  case SDLK_t:
    if (net_connected()) {
      xfer_direction = 0;
      xfer_protocol = 0;
      menu_draw_xfer();
      menu_show();
      kbd_focus = FOCUS_XFER;
    } else {
      menu_draw_message("Not connected.");
      menu_show();
      kbd_focus = FOCUS_REQUESTER;
    }
    break;
    
  case SDLK_v:
    if (macro_len) {
      macro_play = 1;
      macro_ctr = 0;
    }
    break;

  default:
    break;
  }
}


void ui_xferkey(SDL_keysym *keysym) {
  switch (keysym->sym) {

  case SDLK_ESCAPE:
    menu_hide();
    kbd_focus = FOCUS_TERM;
    break;

  case SDLK_1:
    xfer_protocol = PROT_XMODEM1K;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_c:
    xfer_protocol = PROT_XMODEMCRC;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_p:
    xfer_protocol = PROT_PUNTER;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_w:
    xfer_protocol = PROT_RAINBOW;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_m:
    xfer_protocol = PROT_MULTIPUNTER;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_r:
    xfer_direction = DIR_RECV;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_s:
    xfer_direction = DIR_SEND;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_x:
    xfer_protocol = PROT_XMODEM;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (xfer_protocol) {
      if (xfer_direction == DIR_SEND) {
	if (xfer_protocol == PROT_MULTIPUNTER) {
	  kbd_select_files(&select_send_multipunter, FOCUS_REQUESTER);
	} else {
	  kbd_select_file(&select_send_file, FOCUS_REQUESTER);
	}
      } else if (xfer_direction == DIR_RECV) {
	if (xfer_recv()) {
	  if (xfer_protocol == PROT_MULTIPUNTER) {
	    kbd_focus = FOCUS_REQUESTER;
	  } else {
	    ui_inputcall(30, "Enter file name:", xfer_filename, &xfer_save_file, FOCUS_REQUESTER);
	  }
	}
      }
    }
    break;

  default:
    break;
  }
}


void ui_menu(void) {
  menu_print_menu(termmenu);
  menu_show();
  kbd_focus = FOCUS_MENU;
}


void ui_local_init(void) {
  kbd_add_focus(FOCUS_XFER, &ui_xferkey);
  kbd_add_focus(FOCUS_SELECTDIR, &ui_selectdirkey);
}
