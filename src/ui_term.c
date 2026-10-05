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
#include "font.h"
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
#include "modem.h"
#include "music.h"
#include "clipboard.h"
#include "login.h"
#include "session.h"


struct menu termmenu[] = {
  {1,  "",  "-- CONNECTION --"},
  {2,  "B", "Bookmarks"},
  {3,  "D", "Connect/Disconnect"},
  {4,  "R", "Reconnect"},
  {5,  "Y", "History / redial"},
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
  {20, "X", "Copy screen text"},
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
  {31, "O", "Options"},
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
static unsigned char fs_rename_oldraw[16];   /* exact PETSCII name inside an image */


/* ---- disk-image tools in the file selector (V / L / X / I) ------------- */

#ifdef WINDOWS
#define FS_DIRSEP '\\'
#else
#define FS_DIRSEP '/'
#endif

/* 1 while the selector is showing the inside of a .d64/.d71/.d81 */
static int fs_in_image(FileSelector *fs) {
  return fs->dir && fs->dir->blocksfree >= 0;
}

/* the [ SELECT THIS PATH ] / <- Back / [ Go to Home ] lines */
static int fs_is_pseudo(DirEntry *de) {
  return de == NULL || de->name == NULL ||
         strcmp(de->name, FS_SELECT_ENTRY) == 0 ||
         strcmp(de->name, FS_BACK_ENTRY) == 0 ||
         strcmp(de->name, FS_HOME_ENTRY) == 0;
}

/* Show a question and block until a key is pressed: 1 for Y, 0 otherwise.
 * Same event loop as menu_draw_message_timed / menu_select_disk_format. */
static int fs_confirm(const char *question) {
  SDL_Event ev;

  menu_draw_message(question);
  menu_show();
  gfx_vbl();
  for (;;) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        return ev.key.keysym.sym == SDLK_y;
      }
    }
    SDL_Delay(20);
  }
}

/* Redraw the selector after a message / prompt and keep the focus on it. */
static void fs_redraw(void) {
  if (fsel) {
    fs_draw(fsel);
    menu_show();
  }
  kbd_focus = FOCUS_SELECTDIR;
}

/* The folder holding the image being browsed (fsel->path is the image). */
static void fs_image_parent(const char *imagepath, char *out, size_t sz) {
  char *sep;

  snprintf(out, sz, "%s", imagepath);
  sep = strrchr(out, '/');
#ifdef WINDOWS
  { char *bs = strrchr(out, '\\'); if (bs && (sep == NULL || bs > sep)) sep = bs; }
#endif
  if (sep == NULL) {
    snprintf(out, sz, ".");
  } else if (sep == out) {
    out[1] = 0;            /* file directly under the root */
  } else {
    *sep = 0;
  }
}

/* PETSCII directory entry -> host file name, the inverse of the upload
 * rules in xfer.c (pc_to_c64_filename): lowercase, and the CBM file type
 * becomes the extension (PRG -> .prg, SEQ -> .seq, USR -> .usr, REL -> .rel,
 * DEL -> .del). Anything that is not plain printable ASCII, and every
 * character that is special on a host filesystem, becomes '_'. */
static void fs_host_name_from_entry(DirEntry *de, char *out, size_t sz) {
  static const char *ext[] = {".del", ".seq", ".prg", ".usr", ".rel", ".cbm", ".dir", ".bin"};
  char name[20];
  int i, n = 0;

  for (i = 0; i < 16 && de->rawname[i] != 0xa0; i++) {
    unsigned char c = de->rawname[i];
    if (c >= 'A' && c <= 'Z') {
      c += 32;
    } else if (c < 0x20 || c > 0x7e || strchr("/\\:*?\"<>|", c)) {
      c = '_';
    }
    name[n++] = (char)c;
  }
  while (n > 0 && name[n - 1] == ' ') {
    n--;                   /* trailing spaces are invisible on the host */
  }
  name[n] = 0;
  if (n == 0) {
    snprintf(name, sizeof(name), "unnamed");
  } else if (name[0] == '.') {
    name[0] = '_';         /* no hidden files, no "." / ".." */
  }
  snprintf(out, sz, "%s%s", name, ext[de->type & 7]);
}

/* Copy one image entry to a host file in `dir`. Never overwrites: an
 * existing name gets .1, .2, ... appended (as xfer_save_file_in_dir does).
 * hostname receives the name actually used. 1 ok, 0 failed. */
static int fs_extract_entry(DiskImage *di, DirEntry *de, const char *dir, char *hostname, size_t hsz) {
  ImageFile *from;
  FILE *to, *probe;
  unsigned char buf[4096];
  char path[600], alt[620];
  int n, suffix;

  fs_host_name_from_entry(de, hostname, hsz);
  snprintf(path, sizeof(path), "%s%c%s", dir, FS_DIRSEP, hostname);
  if ((probe = fopen(path, "rb")) != NULL) {
    fclose(probe);
    for (suffix = 1; suffix < 1000; suffix++) {
      snprintf(alt, sizeof(alt), "%s.%d", path, suffix);
      if ((probe = fopen(alt, "rb")) == NULL) {
        snprintf(path, sizeof(path), "%s", alt);
        snprintf(alt, sizeof(alt), "%s.%d", hostname, suffix);
        snprintf(hostname, hsz, "%s", alt);
        break;
      }
      fclose(probe);
    }
    if (suffix >= 1000) {
      return 0;
    }
  }
  if ((from = di_open_exact(di, de->rawname)) == NULL) {
    return 0;
  }
  if ((to = fopen(path, "wb")) == NULL) {
    di_close(from);
    return 0;
  }
  while ((n = di_read(from, buf, sizeof(buf))) > 0) {
    if ((int)fwrite(buf, 1, n, to) != n) {
      fclose(to);
      di_close(from);
      remove(path);
      return 0;
    }
  }
  di_close(from);
  if (fclose(to) != 0) {
    remove(path);
    return 0;
  }
  return 1;
}

/* Insert one host file into the image: the file type comes from the
 * extension (.prg/.seq/.usr/.rel, anything else is PRG), the CBM name is
 * the host name without that extension, uppercased and cut to 16 (the same
 * rules pc_to_c64_filename applies for uploads). c64name (>= 17 bytes)
 * receives the name used. Returns 0 ok, 63 file exists, 72 disk full,
 * -1 could not read the host file / other error. */
static int fs_insert_host_file(DiskImage *di, const char *dir, const char *hostname, char *c64name) {
  FILE *from;
  ImageFile *to;
  unsigned char buf[4096], raw[16];
  char base[256], path[600], *dot;
  FileType type = T_PRG;
  long size;
  int n, i;

  snprintf(base, sizeof(base), "%s", hostname);
  dot = strrchr(base, '.');
  if (dot && strlen(dot) == 4) {
    if (strcasecmp(dot, ".prg") == 0)      { type = T_PRG; *dot = 0; }
    else if (strcasecmp(dot, ".seq") == 0) { type = T_SEQ; *dot = 0; }
    else if (strcasecmp(dot, ".usr") == 0) { type = T_USR; *dot = 0; }
    else if (strcasecmp(dot, ".rel") == 0) { type = T_REL; *dot = 0; }
  }
  for (i = 0; base[i]; i++) {
    unsigned char c = (unsigned char)base[i];
    if (c < 0x20 || c > 0x7e || c == '*' || c == '?' || c == ',' || c == ':' || c == '=') {
      base[i] = '-';       /* not representable, or a DOS wildcard / separator */
    }
  }
  base[16] = 0;
  di_rawname_from_name(raw, base);   /* uppercases and pads with 0xA0 */
  di_name_from_rawname(c64name, raw);

  snprintf(path, sizeof(path), "%s%c%s", dir, FS_DIRSEP, hostname);
  if ((from = fopen(path, "rb")) == NULL) {
    return -1;
  }
  fseek(from, 0, SEEK_END);
  size = ftell(from);
  fseek(from, 0, SEEK_SET);
  if (size < 0 || (size + 253) / 254 > di->blocksfree) {
    fclose(from);
    return 72;
  }
  if ((to = di_open(di, raw, type, "wb")) == NULL) {
    fclose(from);
    return di->status == 63 ? 63 : (di->status == 72 ? 72 : -1);
  }
  while ((n = (int)fread(buf, 1, sizeof(buf), from)) > 0) {
    if (di_write(to, buf, n) != n) {
      break;               /* disk full: di_close rolls the entry back */
    }
  }
  di_close(to);
  fclose(from);
  return di->status == 72 ? 72 : 0;
}

/* X inside an image: extract the selected (or every tagged) entry to the
 * folder that holds the image. */
static void fs_extract_files(void) {
  DiskImage *di;
  DirEntry *de;
  char dir[300], hostname[64], msg[128];
  int done = 0, total = 0;

  fs_image_parent(fsel->path, dir, sizeof(dir));
  if ((di = di_load_image(fsel->path)) == NULL) {
    menu_draw_message_timed("Couldn't open disk image", 2000);
    return;
  }
  if (select_mode == SEL_MULTIFILE && fsel->numtagged > 0) {
    for (de = fsel->dir->firstentry; de; de = de->next) {
      if (de->tagged && !fs_is_pseudo(de)) {
        total++;
        done += fs_extract_entry(di, de, dir, hostname, sizeof(hostname));
      }
    }
    snprintf(msg, sizeof(msg), "Extracted %d of %d files", done, total);
  } else {
    de = dir_find(fsel->dir, fsel->current + fsel->offset);
    if (fs_is_pseudo(de) || de->type == T_DIR) {
      di_free_image(di);
      return;
    }
    if (fs_extract_entry(di, de, dir, hostname, sizeof(hostname))) {
      snprintf(msg, sizeof(msg), "Extracted %s", hostname);
    } else {
      snprintf(msg, sizeof(msg), "Could not extract %s", hostname);
    }
  }
  di_free_image(di);   /* read only: nothing to write back */
  menu_draw_message_timed(msg, 2500);
}

/* X in a folder while the download path is an image: insert the selected
 * (or every tagged) host file into that image. */
static void fs_insert_files(void) {
  DiskImage *di;
  DirEntry *de;
  char c64name[20], msg[128];
  int rc = 0, done = 0, total = 0;

  if (!path_is_disk_image(cfg_dldir)) {
    menu_draw_message_timed("Download path is not a disk image (J)", 2500);
    return;
  }
  if ((di = di_load_image(cfg_dldir)) == NULL) {
    menu_draw_message_timed("Couldn't open download disk image", 2500);
    return;
  }
  c64name[0] = 0;
  if (select_mode == SEL_MULTIFILE && fsel->numtagged > 0) {
    for (de = fsel->dir->firstentry; de && rc == 0; de = de->next) {
      if (de->tagged && !fs_is_pseudo(de) && de->type != T_DIR) {
        total++;
        if ((rc = fs_insert_host_file(di, fsel->path, de->name, c64name)) == 0) {
          done++;
        }
      }
    }
    for (; de; de = de->next) {
      if (de->tagged && !fs_is_pseudo(de) && de->type != T_DIR) total++;
    }
  } else {
    de = dir_find(fsel->dir, fsel->current + fsel->offset);
    if (fs_is_pseudo(de) || de->type == T_DIR) {
      di_free_image(di);
      return;
    }
    total = 1;
    if ((rc = fs_insert_host_file(di, fsel->path, de->name, c64name)) == 0) {
      done = 1;
    }
  }
  if (di_free_image(di) != 0) {
    snprintf(msg, sizeof(msg), "Could not write disk image!");
  } else if (rc == 63) {
    snprintf(msg, sizeof(msg), "File exists in image: %s", c64name);
  } else if (rc == 72) {
    snprintf(msg, sizeof(msg), "Disk full, %s not inserted", c64name);
  } else if (rc != 0) {
    snprintf(msg, sizeof(msg), "Could not insert %s", c64name);
  } else if (total == 1) {
    snprintf(msg, sizeof(msg), "Inserted %s into image", c64name);
  } else {
    snprintf(msg, sizeof(msg), "Inserted %d of %d files", done, total);
  }
  menu_draw_message_timed(msg, 2500);
}

/* I: rename-disk flow. The name prompt leads to the ID prompt; ESC in
 * either returns to the selector untouched. */
static char fs_disk_newname[64];

static void fs_disk_id_done(char *id) {
  DiskImage *di;
  unsigned char rawname[16], rawid[2];
  int rc;

  if (fsel == NULL) {
    kbd_focus = FOCUS_TERM;
    return;
  }
  di_rawname_from_name(rawname, fs_disk_newname);
  /* a CBM ID is exactly two characters: pad a short one with a space,
   * keep the old ID when nothing was typed */
  rawid[0] = id[0] ? (unsigned char)toupper((unsigned char)id[0]) : ' ';
  rawid[1] = id[0] && id[1] ? (unsigned char)toupper((unsigned char)id[1]) : ' ';
  if ((di = di_load_image(fsel->path)) == NULL) {
    menu_draw_message_timed("Couldn't open disk image", 2000);
  } else {
    rc = di_rename_disk(di, rawname, id[0] ? rawid : NULL);
    if (di_free_image(di) != 0) {
      menu_draw_message_timed("Could not write disk image!", 3000);
    } else if (rc == 0) {
      menu_draw_message_timed("Disk renamed", 2000);
    }
  }
  fs_read_dir(fsel, fsel->path);
  fs_redraw();
}

static void fs_disk_rename_done(char *name) {
  char id[4];
  int i;

  if (fsel == NULL) {
    kbd_focus = FOCUS_TERM;
    return;
  }
  snprintf(fs_disk_newname, sizeof(fs_disk_newname), "%s", name[0] ? name : (fsel->dir->title ? fsel->dir->title : ""));
  /* offer the current ID as the default when it is printable */
  for (i = 0; i < 2; i++) {
    unsigned char c = fsel->dir->id[i];
    id[i] = (c >= 0x20 && c <= 0x7e) ? (char)c : 0;
  }
  id[2] = 0;
  if (id[0] == 0) id[1] = 0;
  ui_inputcall(2, "New disk ID:", id, &fs_disk_id_done, FOCUS_SELECTDIR);
  ui_inputcall_on_cancel(&fs_redraw);
}

/* I inside an image: name, ID, DOS type, blocks free and file count, then
 * Y leads into the rename prompts. */
static void fs_disk_info(void) {
  DirEntry *de;
  SDL_Event ev;
  char line[80], name[64], id[4], dos[4];
  int files = 0, x, y, rename_it = 0;

  for (de = fsel->dir->firstentry; de; de = de->next) {
    if (!fs_is_pseudo(de)) files++;
  }
  fs_petscii_name_to_display(fsel->dir->title ? fsel->dir->title : "", name, sizeof(name));
  fs_petscii_name_to_display((const char *)fsel->dir->id, id, sizeof(id));
  fs_petscii_name_to_display((const char *)fsel->dir->dostype, dos, sizeof(dos));

  /* menu_draw_message() clears the overlay, draws the boxed title and
   * leaves the body font selected for the lines below it */
  menu_draw_message("DISK INFO");
  x = menu_width / 2 - 140;
  y = menu_height / 2 + 24;
  snprintf(line, sizeof(line), "Name: \"%s\"", name);
  font_draw_string_color(x, y, line, 0xff, 0xff, 0xff);
  snprintf(line, sizeof(line), "ID: %s   DOS: %s", id, dos);
  font_draw_string_color(x, y + 14, line, 0x60, 0x80, 0xff);
  snprintf(line, sizeof(line), "%d blocks free, %d files", fsel->dir->blocksfree, files);
  font_draw_string_color(x, y + 28, line, 0x00, 0xff, 0x66);
  font_draw_string_color(x, y + 48, "Y=rename disk, any key=close", 0x00, 0xee, 0xff);
  menu_show();
  gfx_vbl();
  for (;;) {
    int got = 0;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) exit(0);
      if (ev.type == SDL_KEYDOWN) {
        rename_it = (ev.key.keysym.sym == SDLK_y);
        got = 1;
        break;
      }
    }
    if (got) break;
    SDL_Delay(20);
  }
  if (rename_it) {
    ui_inputcall(16, "New disk name:", name, &fs_disk_rename_done, FOCUS_SELECTDIR);
    ui_inputcall_on_cancel(&fs_redraw);
  } else {
    fs_redraw();
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
	if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, FS_BACK_ENTRY) == 0) {
	  cfg_change_dir(fsel->path, "..");
	} else if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, FS_HOME_ENTRY) == 0) {
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
	if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, FS_BACK_ENTRY) == 0) {
	  cfg_change_dir(fsel->path, "..");
	} else if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, FS_HOME_ENTRY) == 0) {
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

    /* FS_BACK_ENTRY always goes up */
    if (fsel->selectedfile->name && strcmp(fsel->selectedfile->name, FS_BACK_ENTRY) == 0) {
      cfg_change_dir(fsel->path, "..");
      fs_read_dir(fsel, fsel->path);
      fs_draw(fsel);
      break;
    }
    /* FS_SELECT_ENTRY entry — select current directory or image */
    if (fsel->selectedfile->name &&
        strcmp(fsel->selectedfile->name, FS_SELECT_ENTRY) == 0) {
      menu_hide();
      kbd_focus = select_focus;
      select_done_call(fsel);
      fs_free(fsel);
    /* FS_HOME_ENTRY entry — navigate to home directory */
    } else if (fsel->selectedfile->name &&
               strcmp(fsel->selectedfile->name, FS_HOME_ENTRY) == 0) {
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
      int is_image = path_is_disk_image(fsel->path);
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
      if (strcmp(fsel->selectedfile->name, FS_SELECT_ENTRY) == 0 ||
          strcmp(fsel->selectedfile->name, FS_BACK_ENTRY) == 0 ||
          strcmp(fsel->selectedfile->name, FS_HOME_ENTRY) == 0) {
        break;
      }
      {
        char fullpath[512];
        int is_image = path_is_disk_image(fsel->path);

        if (is_image) {
          /* Delete file inside disk image. Match the entry's raw PETSCII
           * name exactly: the listing name may contain '*' or '?', which
           * the DOS wildcard matcher would expand to other files. */
          DiskImage *di = di_load_image(fsel->path);
          if (di) {
            int del_ok = (di_delete_exact(di, fsel->selectedfile->rawname) == 1);
            if (di_free_image(di) != 0) {
              menu_draw_message_timed("Could not write disk image!", 3000);
            } else if (del_ok) {
              menu_draw_message_timed("File deleted from image", 2000);
            } else {
              menu_draw_message_timed("Could not delete file", 2000);
            }
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
      if (strcmp(fsel->selectedfile->name, FS_SELECT_ENTRY) == 0 ||
          strcmp(fsel->selectedfile->name, FS_BACK_ENTRY) == 0 ||
          strcmp(fsel->selectedfile->name, FS_HOME_ENTRY) == 0) {
        break;
      }
      /* Store the old name and ask for new name */
      snprintf(fs_rename_oldname, sizeof(fs_rename_oldname), "%s", fsel->selectedfile->name);
      memcpy(fs_rename_oldraw, fsel->selectedfile->rawname, 16);
      ui_inputcall(30, "New name:", fsel->selectedfile->name, &fs_rename_done, FOCUS_SELECTDIR);
    }
    break;

  case SDLK_v:
    /* Validate the disk image (rebuild the BAM, drop splat files) */
    if (!fs_in_image(fsel)) {
      menu_draw_message_timed("Only available inside a disk image", 2000);
      fs_redraw();
      break;
    }
    if (fs_confirm("Validate disk? (Y/N)")) {
      DiskImage *di = di_load_image(fsel->path);
      int fixed = 0, removed = 0;
      char msg[96];
      if (di == NULL) {
        menu_draw_message_timed("Couldn't open disk image", 2000);
      } else {
        di_validate(di, &fixed, &removed);
        if (di_free_image(di) != 0) {
          menu_draw_message_timed("Could not write disk image!", 3000);
        } else {
          snprintf(msg, sizeof(msg), fixed >= 0 ? "Validated: %d blocks recovered, %d splat entries removed"
                                                : "Validated: %d blocks marked used, %d splat entries removed",
                   fixed >= 0 ? fixed : -fixed, removed);
          menu_draw_message_timed(msg, 3000);
        }
      }
      fs_read_dir(fsel, fsel->path);
    }
    fs_redraw();
    break;

  case SDLK_l:
    /* Lock / unlock the selected file inside an image */
    if (!fs_in_image(fsel)) {
      menu_draw_message_timed("Only available inside a disk image", 2000);
      fs_redraw();
      break;
    }
    fsel->selectedfile = dir_find(fsel->dir, fsel->current + fsel->offset);
    if (!fs_is_pseudo(fsel->selectedfile) && fsel->selectedfile->type != T_DIR) {
      DiskImage *di = di_load_image(fsel->path);
      int lock = !fsel->selectedfile->locked;
      if (di) {
        int rc = di_set_locked(di, fsel->selectedfile->rawname, lock);
        if (di_free_image(di) != 0) {
          menu_draw_message_timed("Could not write disk image!", 3000);
        } else if (rc == 0) {
          menu_draw_message_timed(lock ? "File locked" : "File unlocked", 1500);
        } else {
          menu_draw_message_timed("Could not lock file", 2000);
        }
      }
      {
        /* keep the cursor where it was after the reload */
        int cur = fsel->current, off = fsel->offset;
        fs_read_dir(fsel, fsel->path);
        if (cur + off < fsel->numentries) {
          fsel->current = cur;
          fsel->offset = off;
        }
      }
    }
    fs_redraw();
    break;

  case SDLK_x:
    /* Extract from the image being browsed, or insert into the download
     * image while browsing a folder */
    if (fs_in_image(fsel)) {
      fs_extract_files();
    } else {
      fs_insert_files();
    }
    fs_redraw();
    break;

  case SDLK_i:
    /* Disk info, with the option to rename the disk */
    if (!fs_in_image(fsel)) {
      menu_draw_message_timed("Only available inside a disk image", 2000);
      fs_redraw();
      break;
    }
    fs_disk_info();
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
    int is_image = path_is_disk_image(fsel->path);

    if (is_image) {
      /* Rename inside disk image */
      DiskImage *di = di_load_image(fsel->path);
      if (di) {
        unsigned char new_raw[16];
        int rc;
        di_rawname_from_name(new_raw, newname);
        rc = di_rename_exact(di, fs_rename_oldraw, new_raw);
        if (di_free_image(di) != 0) {
          menu_draw_message_timed("Could not write disk image!", 3000);
        } else if (rc == 0) {
          menu_draw_message_timed("File renamed in image", 2000);
        } else if (rc == 63) {
          menu_draw_message_timed("A file with that name exists", 2000);
        } else {
          menu_draw_message_timed("Could not rename file", 2000);
        }
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
  int ok;
  /* inside a disk image, hand over the exact PETSCII name of the entry */
  const unsigned char *raw = (fs->dir && fs->dir->blocksfree >= 0) ? fs->selectedfile->rawname : NULL;

  if (fs->selectedfile->name == NULL) {
    return;
  }
  ok = xfer_send(fs->selectedfile->name, raw);
  /* Force full redraw of completion message */
  menu_cls();
  menu_draw_message(ok ? "Transfer complete. Press any key." : "Transfer failed. Press any key.");
  menu_show();
  gfx_vbl();
  gfx_vbl();
}


void select_send_multipunter(FileSelector *fs) {
  xfer_send_multipunter(fs);
}


void select_send_zmodem(FileSelector *fs) {
  xfer_send_zmodem(fs);
}


/* The board started sending with ZMODEM (ZRQINIT seen in the stream):
 * receive straight away, files are saved under their own names. */
void ui_autostart_zmodem(void) {
  xfer_protocol = PROT_ZMODEM;
  xfer_direction = DIR_RECV;
  if (xfer_recv()) {
    kbd_focus = FOCUS_REQUESTER;   /* leave the summary on screen */
  } else {
    menu_hide();
    kbd_focus = FOCUS_TERM;
  }
}


/* Options panel: apply one change per key and persist it. */
static void ui_options(void) {
  int key;
  for (;;) {
    char cap[128];
    char val[16];
    if (session_capture_active()) {
      snprintf(cap, sizeof(cap), "on (%s)", session_capture_path());
    } else {
      snprintf(cap, sizeof(cap), "off");
    }
    key = menu_options(cap);
    if (!key) break;
    switch (key) {
    case 's':
      cfg_sound ^= 1;
      cfg_save_setting("sound", cfg_sound ? "yes" : "no");
      music_set_volume(cfg_sound ? cfg_musicvolume : 0);
      break;
    case '+':
    case '-':
      cfg_musicvolume += (key == '+') ? 16 : -16;
      if (cfg_musicvolume < 0) cfg_musicvolume = 0;
      if (cfg_musicvolume > 128) cfg_musicvolume = 128;
      snprintf(val, sizeof(val), "%d", cfg_musicvolume);
      cfg_save_setting("musicvolume", val);
      music_set_volume(cfg_sound ? cfg_musicvolume : 0);
      break;
    case 'd':
    case 'r': {
      static const int steps[] = {0, 5, 10, 20, 50, 100};
      int *v = (key == 'd') ? &cfg_senddelay : &cfg_recvdelay;
      int i, next = 0;
      for (i = 0; i < 6; i++) if (*v == steps[i]) { next = (i + 1) % 6; break; }
      *v = steps[next];
      snprintf(val, sizeof(val), "%d", *v);
      cfg_save_setting(key == 'd' ? "senddelay" : "receivedelay", val);
      break;
    }
    case 'z': {
      int z = cfg_zoom + 1;
      if (z > 4) z = 1;
      if (cfg_columns == 80 && (z == 1 || z == 3)) z++;   /* 80 columns needs an even zoom */
      if (z > 4) z = 2;
      gfx_set_zoom(z);
      snprintf(val, sizeof(val), "%d", cfg_zoom);
      cfg_save_setting("zoom", val);
      break;
    }
    case 'l':
      cfg_statusline ^= 1;
      cfg_save_setting("statusline", cfg_statusline ? "yes" : "no");
      gfx_status_enable(cfg_statusline);
      gfx_set_zoom(cfg_zoom);   /* re-create the window with/without the row */
      break;
    case 'a':
      cfg_autozmodem ^= 1;
      cfg_save_setting("autozmodem", cfg_autozmodem ? "yes" : "no");
      break;
    case 't':
      cfg_transferlog ^= 1;
      cfg_save_setting("transferlog", cfg_transferlog ? "yes" : "no");
      break;
    case 'c':
      if (session_capture_active()) {
        session_capture_close();
      } else {
        session_capture_open(cfg_logfile);
      }
      break;
    case 'e':
      cfg_localecho ^= 1;
      cfg_save_setting("localecho", cfg_localecho ? "yes" : "no");
      break;
    default:
      break;
    }
  }
}


/* Connection history: pick an entry to redial. */
static void ui_history(void) {
  static CfgHistoryEntry entries[40];
  const char *items[40];
  char lines[40][96];
  int n, i, sel;

  n = cfg_read_history(entries, 40);
  if (n == 0) {
    menu_draw_message("No connection history yet");
    menu_show();
    kbd_focus = FOCUS_REQUESTER;
    return;
  }
  for (i = 0; i < n; i++) {
    snprintf(lines[i], sizeof(lines[i]), "%s  %s:%d", entries[i].when, entries[i].host, entries[i].port);
    items[i] = lines[i];
  }
  sel = menu_choose_list("HISTORY", items, n, 0);
  if (sel < 0) {
    menu_hide();
    kbd_focus = FOCUS_TERM;
    return;
  }
  if (net_connected()) {
    net_disconnect();
  }
  menu_hide();
  kbd_focus = FOCUS_TERM;
  cfg_sethost(entries[sel].host);
  cfg_port = entries[sel].port;
  cfg_connect_name[0] = 0;
  {
    int b;
    /* a bookmark for this host carries the terminal mode and the login script */
    for (b = 0; b < cfg_numbookmarks; b++) {
      if (cfg_bookmark_host[b] && strcmp(cfg_bookmark_host[b], entries[sel].host) == 0 &&
          cfg_bookmark_port[b] == entries[sel].port) {
        snprintf(cfg_connect_name, 128, "%s", cfg_bookmark_alias[b] ? cfg_bookmark_alias[b] : "");
        if (cfg_bookmark_termmode[b] == 1 && cfg_termmode == 0) enter_ansi_mode();
        break;
      }
    }
  }
  ffd2(147);
  gfx_vbl();
  if (modem_connect(entries[sel].host, entries[sel].port, &ui_display_net_status) == 0) {
    login_start(entries[sel].host, entries[sel].port);
  }
}


/* Copy the visible screen as plain text to the system clipboard. */
static void ui_copy_screen(void) {
  static char text[25 * 81 + 1];
  gfx_screen_to_text(text, sizeof(text));
  if (clipboard_copy_text(text)) {
    menu_draw_message("Screen text copied to clipboard");
  } else {
    menu_draw_message("Clipboard not available (xclip/xsel/wl-copy?)");
  }
  menu_show();
  kbd_focus = FOCUS_REQUESTER;
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
  case SDLK_o:
    ui_options();
    menu_print_menu(termmenu);
    menu_show();
    kbd_focus = FOCUS_MENU;
    break;
  case SDLK_x:
    ui_copy_screen();
    break;
  case SDLK_y:
    ui_history();
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
      xfer_direction = cfg_lastdirection;
      xfer_protocol = cfg_lastprotocol;
      menu_draw_xfer();
      menu_update_xfer(xfer_direction, xfer_protocol);
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

  case SDLK_z:
    xfer_protocol = PROT_ZMODEM;
    menu_update_xfer(xfer_direction, xfer_protocol);
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (xfer_protocol) {
      /* remember the choice: next time T + Return is enough */
      if (xfer_protocol != cfg_lastprotocol || xfer_direction != cfg_lastdirection) {
	char val[8];
	cfg_lastprotocol = xfer_protocol;
	cfg_lastdirection = xfer_direction;
	snprintf(val, sizeof(val), "%d", cfg_lastprotocol);
	cfg_save_setting("lastprotocol", val);
	snprintf(val, sizeof(val), "%d", cfg_lastdirection);
	cfg_save_setting("lastdirection", val);
      }
      if (xfer_direction == DIR_SEND) {
	if (xfer_protocol == PROT_MULTIPUNTER) {
	  kbd_select_files(&select_send_multipunter, FOCUS_REQUESTER);
	} else if (xfer_protocol == PROT_ZMODEM) {
	  kbd_select_files(&select_send_zmodem, FOCUS_REQUESTER);
	} else {
	  kbd_select_file(&select_send_file, FOCUS_REQUESTER);
	}
      } else if (xfer_direction == DIR_RECV) {
	if (xfer_recv()) {
	  if (xfer_protocol == PROT_MULTIPUNTER || xfer_protocol == PROT_ZMODEM) {
	    kbd_focus = FOCUS_REQUESTER;
	  } else {
	    ui_inputcall(30, "Enter file name:", xfer_filename, (void (*)(char *))&xfer_save_file, FOCUS_REQUESTER);
	    ui_inputcall_on_cancel(&xfer_discard_download);
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
