#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include "diskimage.h"
#include "dir.h"
#include "fileselector.h"
#include "menu.h"


static int filesperpage = 23;  /* default, recalculated in fs_new */


/* Render a raw-PETSCII disk-image filename into a printable ASCII string for
 * the selector. The selector draws with a Latin UI font, so a PETSCII graphics
 * or control byte would otherwise be drawn as an unrelated letter (e.g. 0x72
 * shows as 'r' although on the C64/BBS it is a graphics glyph). Map anything
 * outside the ASCII-compatible PETSCII range (0x20-0x5F: space, digits,
 * punctuation, A-Z, []^_) to '.', so the browser shows an honest approximation
 * of what the board shows instead of a misleading character. Assumes the
 * common uppercase/graphics filename mode. Only for disk-image (PETSCII) names;
 * host-filesystem names are left untouched. */
static void fs_petscii_name_to_display(const char *src, char *dst, size_t dstsz) {
  size_t i = 0;
  if (dstsz == 0) {
    return;
  }
  for (; src[i] && i + 1 < dstsz; ++i) {
    unsigned char c = (unsigned char)src[i];
    dst[i] = (c >= 0x20 && c <= 0x5f) ? (char)c : '.';
  }
  dst[i] = 0;
}


/* Initialize an empty file selector */
FileSelector *fs_new(const char *title, const char *path) {
  FileSelector *fs;

  filesperpage = (menu_height - 70) / 14;
  if (filesperpage < 5) filesperpage = 5;

  if ((fs = malloc(sizeof(*fs))) == NULL) {
    return(NULL);
  }

  fs->title = (char *)title;
  fs->numentries = 0;
  fs->current = 0;
  fs->offset = 0;
  fs->filesperpage = filesperpage;
  fs->dir = NULL;
  snprintf(fs->path, sizeof(fs->path), "%s", path);

  fs->selectedfile = NULL;
  fs->numtagged = 0;

  fs_read_dir(fs, fs->path);
  return(fs);
}


/* Deallocate FileSelector object */
void fs_free(FileSelector *fs) {
  if (fs->dir) {
    dir_free(fs->dir);
  }
  free(fs);
}


signed int fs_read_dir(FileSelector *fs, const char *path) {
  fs->offset = 0;
  fs->current = 0;
  fs->selectedfile = NULL;
  if (fs->numentries) {
    dir_free(fs->dir);
    fs->dir = NULL;
    fs->numentries = 0;
  }
  if ((fs->dir = dir_read(path)) == NULL) {
    /* couldn't read device... */
    char errmsg[256];
    snprintf(errmsg, sizeof(errmsg), "Cannot access path: %s", path);
    menu_draw_message(errmsg);
    menu_show();
    return(-1);
  }
  if (fs->dir->numentries) {
    fs->numentries = fs->dir->numentries;
  } else {
    /* dir_free (not free) — the Dir owns its title/entry allocations; also
     * NULL it so fs_free() doesn't later double-free this pointer. */
    dir_free(fs->dir);
    fs->dir = NULL;
    fs->numentries = 0;
  }
  return(fs->numentries);
}


/* 0=file, 1=dir, 2=disk image, 3=special */
static int fs_entry_type(DirEntry *de) {
  if (de->name && (de->name[0] == '<' || de->name[0] == '[')) return 3;
  if (de->type == T_DIR) {
    /* Check if it's a disk image (.d64/.d71/.d81) shown as dir */
    char *dot = de->name ? strrchr(de->name, '.') : NULL;
    if (dot && strlen(dot) == 4 && (dot[1] == 'd' || dot[1] == 'D') &&
        isdigit(dot[2]) && isdigit(dot[3])) {
      return 2;  /* disk image */
    }
    return 1;  /* regular directory */
  }
  return 0;  /* file */
}

void fs_draw_name(FileSelector *fs, int entry, int line, int selected) {
  DirEntry *de;
  char display[32];
  int etype;

  de = fs->dir->firstentry;
  while (entry--) {
    de = de->next;
  }
  etype = fs_entry_type(de);
  {
    const char *nm = de->name;
    char dn[64];
    if (fs->dir && fs->dir->blocksfree >= 0 && de->name && etype != 3) {
      fs_petscii_name_to_display(de->name, dn, sizeof(dn));
      nm = dn;
    }
    snprintf(display, sizeof(display), "%c%.29s", de->tagged ? '*' : ' ', nm);
  }
  menu_fs_draw_line(line, display, selected || de->tagged, etype, de->size);
}

int fs_toggle_tag(FileSelector *fs, int entry) {
  DirEntry *de;

  de = fs->dir->firstentry;
  while (entry--) {
    de = de->next;
  }
  if (de->type == T_DIR) {
    return fs->numtagged;  /* don't tag directories */
  }
  de->tagged = !de->tagged;
  if (de->tagged) {
    fs->numtagged++;
  } else {
    fs->numtagged--;
  }
  return fs->numtagged;
}


/* Draw file selector */
void fs_draw(FileSelector *fs) {
  DirEntry *de;
  char display[32];
  int l;

  menu_fs_draw(fs->title);
  menu_fs_draw_path(fs->path);

  /* Show blocks free if inside a disk image */
  if (fs->dir && fs->dir->blocksfree >= 0) {
    char bfree[40];
    snprintf(bfree, sizeof(bfree), "%d BLOCKS FREE", fs->dir->blocksfree);
    menu_fs_draw_blocks_free(bfree);
  }

  if (fs->numentries) {
    de = fs->dir->firstentry;
    l = fs->offset;
    while (l--) {
      de = de->next;
    }
    l = 0;
    while (de && l < fs->filesperpage) {
      int etype = fs_entry_type(de);
      {
        const char *nm = de->name;
        char dn[64];
        if (fs->dir && fs->dir->blocksfree >= 0 && de->name && etype != 3) {
          fs_petscii_name_to_display(de->name, dn, sizeof(dn));
          nm = dn;
        }
        snprintf(display, sizeof(display), "%c%.29s", de->tagged ? '*' : ' ', nm);
      }
      menu_fs_draw_line(l, display, (l == fs->current), etype, de->size);
      de = de->next;
      ++l;
    }
  }
}
