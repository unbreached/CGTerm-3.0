#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#ifdef WINDOWS
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif
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
#include "timer.h"
#include "ui.h"
#include "ansi.h"


struct menu termmenu[] = {
  {1,  "",  "-- CONNECTION --"},
  {2,  "B", "Bookmarks"},
  {3,  "D", "Connect/Disconnect"},
  {4,  "R", "Reconnect"},
  {5,  "",  ""},
  {6,  "",  "-- TRANSFERS & FiLES --"},
  {7,  "T", "Transfer file"},
  {9,  "J", "Set paths"},
  {10, "U", "Unjoin disk image"},
  {11, "N", "New disk image"},
  {12, "",  ""},
  {13, "",  "-- SCREEN --"},
  {14, "L", "Load SEQ file"},
  {15, "P", "Post SEQ to BBS"},
  {16, "S", "Save screen"},
  {17, "I", "Screenshot"},
  {17, "Z", "Font settings"},
  {18, "W", "Upper/Lowercase"},
  {19, "G", "Terminal: PETSCII"},
  {20, "",  ""},
  {21, "",  "-- SETTINGS --"},
  {22, "E", "Local echo"},
  {23, "F", "Fullscreen"},
  {24, "K", "Keyboard layout"},
  {25, "H", "Charset"},
  {26, "M", "Oldskool Mode"},
  {27, "C", "Record macro"},
  {28, "V", "Play macro"},
  {29, "A", "Abort"},
  {30, "?", "Help"},
  {31, "",  ""},
  {32, "Q", "Quit CGTerm"},
  {0, NULL, NULL}
};


static int find_menu_key(struct menu *m, const char *key) {
  int i;
  for (i = 0; m[i].key != NULL; i++) {
    if (strcmp(m[i].key, key) == 0) return i;
  }
  return 0;
}




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


/* Forward declarations for file selector operations */
static void fs_create_dir_done(char *dirname);
static void fs_rename_done(char *newname);
static char fs_rename_oldname[256];

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
	if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, "<- Back") == 0) {
	  cfg_change_dir(fsel->path, "..");
	} else if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, "[ Go to Home ]") == 0) {
	  const char *homedir = NULL;
#ifdef WINDOWS
	  /* Try USERPROFILE first, then HOMEDRIVE+HOMEPATH */
	  homedir = getenv("USERPROFILE");
	  if (!homedir) {
	    const char *homedrive = getenv("HOMEDRIVE");
	    const char *homepath = getenv("HOMEPATH");
	    static char win_home_path[512];
	    if (homedrive && homepath) {
	      snprintf(win_home_path, sizeof(win_home_path), "%s%s", homedrive, homepath);
	      homedir = win_home_path;
	    }
	  }
#else
	  homedir = getenv("HOME");
#endif
	  if (homedir && strlen(homedir) > 0) {
	    snprintf(fsel->path, sizeof(fsel->path), "%s", homedir);
	  } else {
	    menu_draw_message("Home directory not found");
	    menu_show();
	  }
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
	if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, "<- Back") == 0) {
	  cfg_change_dir(fsel->path, "..");
	} else if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, "[ Go to Home ]") == 0) {
	  const char *homedir = NULL;
#ifdef WINDOWS
	  /* Try USERPROFILE first, then HOMEDRIVE+HOMEPATH */
	  homedir = getenv("USERPROFILE");
	  if (!homedir) {
	    const char *homedrive = getenv("HOMEDRIVE");
	    const char *homepath = getenv("HOMEPATH");
	    static char win_home_path[512];
	    if (homedrive && homepath) {
	      snprintf(win_home_path, sizeof(win_home_path), "%s%s", homedrive, homepath);
	      homedir = win_home_path;
	    }
	  }
#else
	  homedir = getenv("HOME");
#endif
	  if (homedir && strlen(homedir) > 0) {
	    snprintf(fsel->path, sizeof(fsel->path), "%s", homedir);
	  } else {
	    menu_draw_message("Home directory not found");
	    menu_show();
	  }
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

    /* In MULTIFILE mode: if files are tagged, Enter always sends them */
    if (select_mode == SEL_MULTIFILE && fsel->numtagged > 0) {
      /* Update xferdir to the current path — critical when inside a D64 */
      snprintf(cfg_xferdir, 256, "%s", fsel->path);
      snprintf(cfg_dldir, 256, "%s", fsel->path);
      menu_hide();
      kbd_focus = select_focus;
      select_done_call(fsel);
      fs_free(fsel);
      break;
    }

    /* "<- Back" always goes up */
    if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, "<- Back") == 0) {
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
    /* "[ Go to Home ]" entry — navigate to home directory */
    } else if (fsel->selectedfile->name &&
               strcmp(fsel->selectedfile->name, "[ Go to Home ]") == 0) {
      const char *homedir = NULL;
#ifdef WINDOWS
      /* Try USERPROFILE first, then HOMEDRIVE+HOMEPATH */
      homedir = getenv("USERPROFILE");
      if (!homedir) {
        const char *homedrive = getenv("HOMEDRIVE");
        const char *homepath = getenv("HOMEPATH");
        static char win_home_path2[512];
        if (homedrive && homepath) {
          snprintf(win_home_path2, sizeof(win_home_path2), "%s%s", homedrive, homepath);
          homedir = win_home_path2;
        }
      }
#else
      homedir = getenv("HOME");
#endif
      if (homedir) {
        snprintf(fsel->path, sizeof(fsel->path), "%s", homedir);
        fs_read_dir(fsel, fsel->path);
        fs_draw(fsel);
      }
    } else if (fsel->selectedfile->type == T_DIR) {
      cfg_change_dir(fsel->path, fsel->selectedfile->name);
      fs_read_dir(fsel, fsel->path);
      fs_draw(fsel);
    } else if (select_mode == SEL_DIR) {
      menu_hide();
      kbd_focus = select_focus;
      select_done_call(fsel);
      fs_free(fsel);
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

  case SDLK_LEFT:
    /* Page up */
    if (fsel->offset > 0) {
      fsel->offset -= fsel->filesperpage;
      if (fsel->offset < 0) fsel->offset = 0;
      fsel->current = 0;
      fs_draw(fsel);
    } else {
      fsel->current = 0;
      fs_draw(fsel);
    }
    break;

  case SDLK_RIGHT:
    /* Page down */
    if (fsel->offset + fsel->filesperpage < fsel->numentries) {
      fsel->offset += fsel->filesperpage;
      fsel->current = 0;
      fs_draw(fsel);
    }
    break;

  case SDLK_F2:
    /* Save current path as default */
    {
      FILE *cf;
      char cfname[512];
#ifdef WINDOWS
      snprintf(cfname, sizeof(cfname), "cgterm.cfg");
#else
      snprintf(cfname, sizeof(cfname), "%s/.cgtermrc", cfg_homedir);
#endif
      cf = fopen(cfname, "a");
      if (cf) {
        fprintf(cf, "\ndldir = %s\n", fsel->path);
        fclose(cf);
        snprintf(cfg_dldir, 256, "%s", fsel->path);
        snprintf(cfg_xferdir, 256, "%s", fsel->path);
        menu_draw_message_timed("Path saved as default!", 2000);
      } else {
        menu_draw_message_timed("Could not save config", 2000);
      }
      fs_draw(fsel);
      menu_show();
    }
    break;

  case SDLK_c:
    /* Create new directory (not inside disk images) */
    {
      char *p = strrchr(fsel->path, '.');
      int is_image = (p && strlen(p) == 4 && (p[1] == 'd' || p[1] == 'D')
                      && isdigit(p[2]) && isdigit(p[3]));
      if (is_image) {
        menu_draw_message_timed("Can't create folders inside disk images", 3000);
      } else {
        ui_inputcall(30, "New folder name:", "", &fs_create_dir_done, FOCUS_SELECTDIR);
      }
    }
    break;

  case SDLK_d:
    /* Delete selected file or directory */
    if (fsel->selectedfile == NULL)
      fsel->selectedfile = dir_find(fsel->dir, fsel->current + fsel->offset);
    if (fsel->selectedfile && fsel->selectedfile->name) {
      /* Don't delete special entries */
      if (strcmp(fsel->selectedfile->name, "[ Use this folder ]") == 0 ||
          strcmp(fsel->selectedfile->name, "<- Back") == 0 ||
          strcmp(fsel->selectedfile->name, "[ Go to Home ]") == 0) {
        break;
      }
      {
        char fullpath[512];
        char *p = strrchr(fsel->path, '.');
        int is_image = (p && strlen(p) == 4 && (p[1] == 'd' || p[1] == 'D')
                        && isdigit(p[2]) && isdigit(p[3]));

        if (is_image) {
          /* Delete file inside disk image */
          DiskImage *di = di_load_image(fsel->path);
          if (di) {
            unsigned char rawname[16];
            di_rawname_from_name(rawname, fsel->selectedfile->name);
            {
              int del_ok = 0;
              FileType ftypes[] = {T_PRG, T_SEQ, T_USR, T_REL};
              int ft;
              for (ft = 0; ft < 4 && !del_ok; ft++) {
                if (di_delete(di, rawname, ftypes[ft]) == 0)
                  del_ok = 1;
              }
              if (del_ok)
                menu_draw_message_timed("File deleted from image", 2000);
              else
                menu_draw_message_timed("Could not delete file", 2000);
            }
            di_free_image(di);
          }
        } else {
          /* Delete from filesystem */
          snprintf(fullpath, sizeof(fullpath), "%s%c%s", fsel->path,
#ifdef WINDOWS
            '\\',
#else
            '/',
#endif
            fsel->selectedfile->name);
          if (fsel->selectedfile->type == T_DIR) {
            if (rmdir(fullpath) == 0) {
              menu_draw_message_timed("Directory deleted", 2000);
            } else {
              menu_draw_message_timed("Could not delete (not empty?)", 2000);
            }
          } else {
            if (remove(fullpath) == 0) {
              menu_draw_message_timed("File deleted", 2000);
            } else {
              menu_draw_message_timed("Could not delete file", 2000);
            }
          }
        }
        /* Refresh directory listing */
        fs_read_dir(fsel, fsel->path);
        fs_draw(fsel);
      }
    }
    break;

  case SDLK_r:
    /* Rename selected file */
    if (fsel->selectedfile == NULL)
      fsel->selectedfile = dir_find(fsel->dir, fsel->current + fsel->offset);
    if (fsel->selectedfile && fsel->selectedfile->name) {
      if (strcmp(fsel->selectedfile->name, "[ Use this folder ]") == 0 ||
          strcmp(fsel->selectedfile->name, "<- Back") == 0 ||
          strcmp(fsel->selectedfile->name, "[ Go to Home ]") == 0) {
        break;
      }
      /* Store the old name and ask for new name */
      snprintf(fs_rename_oldname, sizeof(fs_rename_oldname), "%s", fsel->selectedfile->name);
      ui_inputcall(30, "New name:", fsel->selectedfile->name, &fs_rename_done, FOCUS_SELECTDIR);
    }
    break;

  default:
    break;
  }
}


/* File selector: create directory callback */
static void fs_create_dir_done(char *dirname) {
  if (fsel && dirname[0]) {
    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "%s%c%s", fsel->path,
#ifdef WINDOWS
      '\\',
#else
      '/',
#endif
      dirname);
#ifdef WINDOWS
    if (_mkdir(fullpath) == 0) {
#else
    if (mkdir(fullpath, 0755) == 0) {
#endif
      menu_draw_message_timed("Directory created", 2000);
    } else {
      menu_draw_message_timed("Could not create directory", 2000);
    }
    fs_read_dir(fsel, fsel->path);
    fs_draw(fsel);
    menu_show();
  }
  kbd_focus = FOCUS_SELECTDIR;
}

static void fs_rename_done(char *newname) {
  if (fsel && newname[0] && strcmp(newname, fs_rename_oldname) != 0) {
    char *p = strrchr(fsel->path, '.');
    int is_image = (p && strlen(p) == 4 && (p[1] == 'd' || p[1] == 'D')
                    && isdigit(p[2]) && isdigit(p[3]));

    if (is_image) {
      /* Rename inside disk image */
      DiskImage *di = di_load_image(fsel->path);
      if (di) {
        unsigned char old_raw[16], new_raw[16];
        di_rawname_from_name(old_raw, fs_rename_oldname);
        di_rawname_from_name(new_raw, newname);
        {
          int ren_ok = 0;
          FileType ftypes[] = {T_PRG, T_SEQ, T_USR, T_REL};
          int ft;
          for (ft = 0; ft < 4 && !ren_ok; ft++) {
            if (di_rename(di, old_raw, new_raw, ftypes[ft]) == 0)
              ren_ok = 1;
          }
          if (ren_ok)
            menu_draw_message_timed("File renamed in image", 2000);
          else
            menu_draw_message_timed("Could not rename file", 2000);
        }
        di_free_image(di);
      }
    } else {
      /* Rename on filesystem */
      char oldpath[512], newpath[512];
      snprintf(oldpath, sizeof(oldpath), "%s%c%s", fsel->path,
#ifdef WINDOWS
        '\\',
#else
        '/',
#endif
        fs_rename_oldname);
      snprintf(newpath, sizeof(newpath), "%s%c%s", fsel->path,
#ifdef WINDOWS
        '\\',
#else
        '/',
#endif
        newname);
      if (rename(oldpath, newpath) == 0) {
        menu_draw_message_timed("Renamed successfully", 2000);
      } else {
        menu_draw_message_timed("Could not rename", 2000);
      }
    }
    fs_read_dir(fsel, fsel->path);
    fs_draw(fsel);
    menu_show();
  }
  kbd_focus = FOCUS_SELECTDIR;
}


/* Post SEQ file to BBS at 300bps */
static void select_post_seq(FileSelector *fs) {
  char fullpath[512];
  FILE *f;
  int c;
  int count = 0;
  long filesize = 0;
  int delay_ms = 33;  /* default 300bps */
  const char *speed_name = "300";
  SDL_Event ev;

  if (!fs->selectedfile || !fs->selectedfile->name) return;

  snprintf(fullpath, sizeof(fullpath), "%s%c%s", fs->path,
#ifdef WINDOWS
    '\\',
#else
    '/',
#endif
    fs->selectedfile->name);

  f = fopen(fullpath, "rb");
  if (!f) {
    menu_draw_message_timed("Could not open file", 3000);
    return;
  }

  /* Get file size for progress bar */
  fseek(f, 0, SEEK_END);
  filesize = ftell(f);
  fseek(f, 0, SEEK_SET);

  /* Speed selection via menu */
  {
    int speed_sel = menu_select_post_speed();
    const int delays[] = {33, 8, 4, 2, 1};
    const char *snames[] = {"300", "1200", "2400", "4800", "9600"};
    if (speed_sel < 0) {
      fclose(f);
      return;
    }
    delay_ms = delays[speed_sel];
    speed_name = snames[speed_sel];
  }

  /* Post with progress bar and PETSCII preview */
  menu_xfer_set_seq_preview(1);
  {
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "Posting SEQ at %s bps", speed_name);
    menu_draw_xfer_progress(fs->selectedfile->name, 1, 0);
    menu_update_xfer_progress(hdr, 0, (int)filesize);
    menu_show();
    gfx_vbl();
  }

  while ((c = fgetc(f)) != EOF) {
    net_send((unsigned char)c);
    menu_xfer_feed_byte((unsigned char)c);
    count++;

    /* Update progress every 20 bytes with fun messages */
    if (count % 20 == 0) {
      static const char *seq_msgs[] = {
        "Drawing your PETSCii masterpiece...",
        "Transmitting elite artwork...",
        "Painting pixels at %s bps...",
        "Beaming PETSCii to the BBS...",
        "Making the sysop jealous...",
        "Uploading scene art...",
        "Converting pixels to glory...",
        "Sending digital graffiti..."
      };
      char msg[64];
      int mi = (count / 200) % 8;
      snprintf(msg, sizeof(msg), seq_msgs[mi], speed_name);
      menu_update_xfer_progress(msg, count, (int)filesize);
      menu_show();
      gfx_vbl();
    }

    timer_delay(delay_ms);

    /* Check for ESC to abort */
    if (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
        break;
      }
    }
  }

  fclose(f);
  menu_xfer_set_seq_preview(0);

  {
    char msg[64];
    snprintf(msg, sizeof(msg), "Posted %d bytes", count);
    menu_draw_message_timed(msg, 3000);
  }
}


/* Save screen — pick directory then filename */
static char save_screen_dir[256];

static void save_screen_filename(char *filename) {
  char fullpath[512];
  snprintf(fullpath, sizeof(fullpath), "%s%c%s", save_screen_dir,
#ifdef WINDOWS
    '\\',
#else
    '/',
#endif
    filename);
  gfx_savescreen(fullpath);
  menu_draw_message_timed("Screen saved!", 3000);
}

static void save_screen_dir_done(FileSelector *fs) {
  snprintf(save_screen_dir, sizeof(save_screen_dir), "%s", fs->path);
  ui_inputcall(30, "Filename:", "screen.seq", &save_screen_filename, FOCUS_TERM);
}


void select_set_xferdir(FileSelector *fs) {
  snprintf(cfg_xferdir, 256, "%s", fs->path);
}

void select_set_dldir(FileSelector *fs) {
  /* Set both download and upload paths to the same location */
  snprintf(cfg_dldir, 256, "%s", fs->path);
  snprintf(cfg_xferdir, 256, "%s", fs->path);
  cfg_save_setting("xferdir", cfg_dldir);
}

static void select_set_upload_path(FileSelector *fs) {
  snprintf(cfg_xferdir, 256, "%s", fs->path);
  cfg_save_setting("xferdir", cfg_xferdir);
}

static void select_set_download_path(FileSelector *fs) {
  snprintf(cfg_dldir, 256, "%s", fs->path);
  cfg_save_setting("dldir", cfg_dldir);
}

static void select_set_seq_path(FileSelector *fs) {
  snprintf(cfg_seqdir, 256, "%s", fs->path);
  cfg_save_setting("seqdir", cfg_seqdir);
}

static void select_set_screen_path(FileSelector *fs) {
  snprintf(cfg_screendir, 256, "%s", fs->path);
  cfg_save_setting("screendir", cfg_screendir);
}


/* New disk image creation */
static char new_d64_path[512];
static int new_d64_size = 174848;  /* default D64 */
static const char *new_d64_ext = ".d64";

void ui_create_d64_label(char *label) {
  DiskImage *di;
  unsigned char rawname[16];
  unsigned char rawid[2] = { '0', '0' };
  char msg[560];

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


void select_load_seq(FileSelector *fs) {
  char fullpath[512];
  snprintf(fullpath, sizeof(fullpath), "%s%c%s", fs->path,
#ifdef WINDOWS
    '\\',
#else
    '/',
#endif
    fs->selectedfile->name);
  kbd_loadseq(fullpath);
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
  /* Unicode-based help trigger — works on any keyboard layout */
  if (keysym->unicode == '?') {
    char fname[1024];
    path_build_asset(fname, sizeof(fname), "help.txt");
    menu_show_help(fname);
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    return;
  }
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
      menu_draw_bookmarks_sel(ui_get_bookmark_cursor());
      menu_show();
      kbd_focus = FOCUS_BOOKMARKS;
    }
    break;
    
  case SDLK_c:
    if (macro_rec) {
      macro_rec = 0;
    } else {
      macro_play = 0;   /* don't start recording over an in-progress playback */
      macro_ctr = 0;
      macro_len = 0;
      macro_rec = 1;
    }
    break;

  case SDLK_d:
    if (net_connected()) {
      net_disconnect();
    } else {
      /* Pre-fill with host:port if we have both */
      {
        char hostbuf[256] = "";
        if (cfg_host) {
          snprintf(hostbuf, sizeof(hostbuf), "%s %d", cfg_host, cfg_port);
        }
        ui_inputcall(30, "Connect to host [port]:", hostbuf, &ui_connect, FOCUS_TERM);
      }
    }
    break;

  case SDLK_e:
    cfg_localecho ^= 1;
    cfg_save_setting("localecho", cfg_localecho ? "yes" : "no");
    break;

  case SDLK_f:
    gfx_toggle_fullscreen();
    cfg_save_setting("fullscreen", cfg_fullscreen ? "yes" : "no");
    break;

  case SDLK_h:
    /* Cycle charset: US/UK -> Swedish -> German -> US/UK */
    cfg_charset = (cfg_charset + 1) % 3;
    gfx_reload_charset();
    cfg_save_setting("charset", cfg_charset == 1 ? "swedish" : cfg_charset == 2 ? "german" : "us");
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;

  case SDLK_g:
    if (cfg_termmode == 0) {
      enter_ansi_mode();
      termmenu[find_menu_key(termmenu, "G")].text = "Terminal: ANSI";
    } else {
      enter_petscii_mode();
      termmenu[find_menu_key(termmenu, "G")].text = "Terminal: PETSCII";
    }
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;

  case SDLK_m:
    cfg_modem ^= 1;
    cfg_save_setting("modem", cfg_modem ? "yes" : "no");
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;

  case SDLK_k:
    menu_keyboard_test();
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;

  case SDLK_w:
    gfx_toggle_font();
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;

  case SDLK_z:
    menu_select_menu_font();
    menu_select_splash_font();
    {
      char val[8];
      snprintf(val, sizeof(val), "%d", cfg_menufont);
      cfg_save_setting("menufont", val);
      snprintf(val, sizeof(val), "%d", cfg_splashfont);
      cfg_save_setting("splashfont", val);
    }
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;

  case SDLK_j:
    {
      int choice = menu_set_paths();
      switch (choice) {
      case 'u':
        kbd_select_dir_from(&select_set_upload_path, FOCUS_TERM, "Upload path", cfg_xferdir);
        break;
      case 'd':
        kbd_select_dir_from(&select_set_download_path, FOCUS_TERM, "Download path", cfg_dldir);
        break;
      case 's':
        kbd_select_dir_from(&select_set_seq_path, FOCUS_TERM, "SEQ save path", cfg_seqdir);
        break;
      case 'p':
        kbd_select_dir_from(&select_set_screen_path, FOCUS_TERM, "Screenshot path", cfg_screendir);
        break;
      default:
        menu_hide();
        kbd_focus = FOCUS_TERM;
        break;
      }
    }
    break;

  case SDLK_l:
    kbd_select_file(&select_load_seq, FOCUS_TERM);
    break;

  case SDLK_p:
    /* Post SEQ file to BBS at 300bps */
    if (net_connected()) {
      kbd_select_file(&select_post_seq, FOCUS_TERM);
    } else {
      menu_draw_message("Not connected.");
      menu_show();
      kbd_focus = FOCUS_REQUESTER;
    }
    break;

  case SDLK_i:
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
      snprintf(bmppath, sizeof(bmppath), "%s\\%s", cfg_screendir, bmpname);
#else
      snprintf(bmppath, sizeof(bmppath), "%s/%s", cfg_screendir, bmpname);
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
    gfx_crt_shutdown();
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
    kbd_select_dir_from(&save_screen_dir_done, FOCUS_TERM, "Save screen where?", cfg_seqdir);
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
