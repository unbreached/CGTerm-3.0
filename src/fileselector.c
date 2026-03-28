#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "diskimage.h"
#include "dir.h"
#include "fileselector.h"
#include "menu.h"


static int filesperpage = 23;  /* fits in 640x400 menu surface */


/* Initialize an empty file selector */
FileSelector *fs_new(const char *title, const char *path) {
  FileSelector *fs;

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
    fs->numentries = 0;
  }
  if ((fs->dir = dir_read(path)) == NULL) {
    /* couldn't read device... */
    puts("fs_read_dir failed!");
    return(-1);
  }
  if (fs->dir->numentries) {
    fs->numentries = fs->dir->numentries;
  } else {
    free(fs->dir);
    fs->numentries = 0;
  }
  return(fs->numentries);
}


void fs_draw_name(FileSelector *fs, int entry, int line, int selected) {
  DirEntry *de;
  char display[32];

  de = fs->dir->firstentry;
  while (entry--) {
    de = de->next;
  }
  if (de->tagged) {
    snprintf(display, sizeof(display), "*%.29s", de->name);
  } else {
    snprintf(display, sizeof(display), " %.29s", de->name);
  }
  menu_fs_draw_line(line, display, selected || de->tagged, de->type == T_DIR ? 1 : 0);
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

  if (fs->numentries) {
    de = fs->dir->firstentry;
    l = fs->offset;
    while (l--) {
      de = de->next;
    }
    l = 0;
    while (de && l < fs->filesperpage) {
      if (de->tagged) {
        snprintf(display, sizeof(display), "*%.29s", de->name);
      } else {
        snprintf(display, sizeof(display), " %.29s", de->name);
      }
      menu_fs_draw_line(l, display, (l == fs->current) || de->tagged, de->type == T_DIR ? 1 : 0);
      de = de->next;
      ++l;
    }
  }
}
