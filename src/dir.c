#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef WINDOWS
#include <windows.h>
#endif
#include "diskimage.h"
#include "dir.h"


/* 8 entries: the type nibble is masked with 7, so index 7 must exist */
char *dir_type[] = {"DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "???"};


int path_is_disk_image(const char *path) {
  const char *p;

  if (path == NULL || (p = strrchr(path, '.')) == NULL || strlen(p) != 4) {
    return 0;
  }
  return (p[1] == 'd' || p[1] == 'D') &&
         isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]);
}


/* convert A0 padded petscii name to ascii */
char *make_name(unsigned char *rawname) {
  int l;
  char *name;

  /* bound the index BEFORE dereferencing: rawname is a fixed 16-byte field
     with no guaranteed terminator, so test l < 16 first. */
  for (l = 0; l < 16 && rawname[l] != 0xa0; ++l);
  if ((name = malloc(l+1)) == NULL) {
    return(NULL);
  }
  memcpy(name, rawname, l);
  name[l] = 0;
  return(name);
}


/* read directory */
Dir *dir_read_image(DiskImage *di) {
  unsigned char buffer[254];
  ImageFile *fh;
  Dir *dir;
  int offset;
  DirEntry *entry = NULL;

  if ((fh = di_open(di, (const unsigned char *)"$", T_PRG, "rb")) == NULL) {
    return(NULL);
  }

  if ((dir = malloc(sizeof(*dir))) == NULL) {
    printf("couldn't allocate dir structure\n");
    goto ReadDirDone;
  }

  dir->numentries = 0;
  dir->title = NULL;
  dir->firstentry = NULL;
  dir->blocksfree = di->blocksfree;
  memcpy(dir->id, di_id(di), 2);
  dir->id[2] = 0;
  memcpy(dir->dostype, di_dostype(di), 2);
  dir->dostype[2] = 0;

  if (di_read(fh, buffer, 254) != 254) {
    printf("BAM read failed\n");
    goto ReadDirDone;
  }

  dir->title = make_name(di_title(di));

  /* Add "[ Use this image ]" to select the disk image as target */
  {
    DirEntry *use_entry;
    if ((use_entry = malloc(sizeof(*use_entry))) != NULL) {
      use_entry->prev = NULL;
      use_entry->next = NULL;
      if ((use_entry->name = malloc(24))) {
        strcpy(use_entry->name, FS_SELECT_ENTRY);
      }
      memset(use_entry->rawname, 0xa0, 16);
      use_entry->type = T_SEQ;
      use_entry->closed = 1;
      use_entry->locked = 0;
      use_entry->track = 0;
      use_entry->sector = 0;
      use_entry->size = 0;
      use_entry->tagged = 0;
      use_entry->mtime = 0;
      dir->firstentry = use_entry;
      entry = use_entry;
      dir->numentries = 1;
    }
  }

  /* Add <- Back (go back) */
  {
    DirEntry *back_entry;
    if ((back_entry = malloc(sizeof(*back_entry))) != NULL) {
      back_entry->prev = entry;
      back_entry->next = NULL;
      if (entry) entry->next = back_entry;
      if ((back_entry->name = malloc(8))) {
        strcpy(back_entry->name, "<- Back");
      }
      memset(back_entry->rawname, 0xa0, 16);
      back_entry->rawname[0] = '.';
      back_entry->type = T_DIR;
      back_entry->closed = 1;
      back_entry->locked = 0;
      back_entry->track = 0;
      back_entry->sector = 0;
      back_entry->size = 0;
      back_entry->tagged = 0;
      back_entry->mtime = 0;
      entry = back_entry;
      dir->numentries = 2;
    }
  }

  /* both header entries failed to allocate: nothing to hang entries off */
  if (entry == NULL) {
    goto ReadDirDone;
  }

  while (di_read(fh, buffer, 254) == 254) {
    for (offset = -2; offset < 254; offset += 32) {
      if (buffer[offset+2]) {
	//if (dir->numentries) {
	if ((entry->next = malloc(sizeof(*(entry->next)))) == NULL) {
	  goto ReadDirDone;
	}
	entry->next->prev = entry;
	entry = entry->next;
	  /*} else {
	  if ((dir->firstentry = malloc(sizeof(*(dir->firstentry)))) == NULL) {
	    goto ReadDirDone;
	  }
	  entry = dir->firstentry;
	  entry->prev = NULL;
	  }*/
	entry->next = NULL;
	entry->name = make_name(buffer + (offset + 5));   /* offset may be -2: keep the index in bounds */
	memcpy(entry->rawname, buffer + (offset + 5), 16);
	entry->type = buffer[offset + 2] & 7;
	entry->closed = buffer[offset + 2] & 0x80;
	entry->locked = buffer[offset + 2] & 0x40;
	entry->track = buffer[offset + 3];
	entry->sector = buffer[offset + 4];
	entry->size = buffer[offset + 31]<<8 | buffer[offset + 30];
	entry->tagged = 0;
	entry->mtime = 0;
	++(dir->numentries);
      }
    }
  }

 ReadDirDone:
  di_close(fh);
  return(dir);
}


/* Sort order: real directories (alphabetical), then disk images
 * (alphabetical), then files newest first. */
static int dir_entry_compare(const void *pa, const void *pb) {
  const DirEntry *a = *(DirEntry *const *)pa;
  const DirEntry *b = *(DirEntry *const *)pb;
  int ca = 2, cb = 2;

  if (a->type == T_DIR) ca = path_is_disk_image(a->name) ? 1 : 0;
  if (b->type == T_DIR) cb = path_is_disk_image(b->name) ? 1 : 0;
  if (ca != cb) return ca - cb;
  if (ca < 2) {
    if (a->name && b->name) return strcasecmp(a->name, b->name);
    return 0;
  }
  if (a->mtime != b->mtime) return (a->mtime > b->mtime) ? -1 : 1;
  if (a->name && b->name) return strcasecmp(a->name, b->name);
  return 0;
}


Dir *dir_read_opendir(DIR *dirhandle, const char *path) {
  Dir *dir;
  DirEntry *entry = NULL;
  struct dirent *dirent;
  unsigned namelen;
#ifdef WINDOWS
  DIR *d;
  char namebuf[1024];
  char *name;
  int len;

  /* Reject paths that would leave no room for a separator + filename. */
  len = (int)strlen(path);
  if (len < 0 || len >= (int)sizeof(namebuf) - 260) {
    return NULL;
  }
  memcpy(namebuf, path, len);
  namebuf[len++] = '\\';
  name = namebuf + len;
#endif

  if ((dir = malloc(sizeof(*dir))) == NULL) {
    printf("couldn't allocate dir structure\n");
    return(NULL);
  }

  dir->numentries = 0;
  dir->title = NULL;
  dir->firstentry = NULL;
  dir->blocksfree = -1;  /* not a disk image */
  dir->id[0] = 0;
  dir->dostype[0] = 0;

  if ((dir->title = malloc(strlen(path) + 1))) {
    strcpy(dir->title, path);
  }

  /* Add "[ Use this folder ]" as first entry */
  {
    DirEntry *use_entry;
    if ((use_entry = malloc(sizeof(*use_entry))) != NULL) {
      use_entry->prev = NULL;
      use_entry->next = NULL;
      if ((use_entry->name = malloc(24))) {
        strcpy(use_entry->name, FS_SELECT_ENTRY);
      }
      memset(use_entry->rawname, 0xa0, 16);
      use_entry->type = T_SEQ;  /* special type — not DIR, not PRG */
      use_entry->closed = 1;
      use_entry->locked = 0;
      use_entry->track = 0;
      use_entry->sector = 0;
      use_entry->size = 0;
      use_entry->tagged = 0;
      use_entry->mtime = 0;
      dir->firstentry = use_entry;
      entry = use_entry;
      dir->numentries = 1;
    }
  }

  /* Add "." entry for going back */
  if (dir->firstentry) {
    DirEntry *dotdot;
    if ((dotdot = malloc(sizeof(*dotdot))) != NULL) {
      dotdot->prev = entry;
      dotdot->next = NULL;
      entry->next = dotdot;
      entry = dotdot;
      if ((dotdot->name = malloc(8))) {
        strcpy(dotdot->name, "<- Back");
      }
      memset(dotdot->rawname, 0xa0, 16);
      dotdot->rawname[0] = '.';
      dotdot->rawname[1] = '.';
      dotdot->type = T_DIR;
      dotdot->closed = 1;
      dotdot->locked = 0;
      dotdot->track = 0;
      dotdot->sector = 0;
      dotdot->size = 0;
      dotdot->tagged = 0;
      dotdot->mtime = 0;
      dir->numentries = 2;
    }
  }

  /* Add "Go to Home" entry if we're not already in home directory */
  {
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
    /* Only add if we have a valid home directory and we're not already there */
    if (homedir && strlen(homedir) > 0 && strcmp(path, homedir) != 0) {
      DirEntry *home_entry;
      if ((home_entry = malloc(sizeof(*home_entry))) != NULL) {
        home_entry->prev = entry;
        home_entry->next = NULL;
        if (entry) {
          entry->next = home_entry;
        } else {
          dir->firstentry = home_entry;
        }
        entry = home_entry;
        if ((home_entry->name = malloc(16))) {
          strcpy(home_entry->name, FS_HOME_ENTRY);
        }
        memset(home_entry->rawname, 0xa0, 16);
        home_entry->rawname[0] = 'H';
        home_entry->rawname[1] = 'O';
        home_entry->rawname[2] = 'M';
        home_entry->rawname[3] = 'E';
        home_entry->type = T_SEQ;  /* special type */
        home_entry->closed = 1;
        home_entry->locked = 0;
        home_entry->track = 0;
        home_entry->sector = 0;
        home_entry->size = 0;
        home_entry->tagged = 0;
        home_entry->mtime = 0;
        dir->numentries++;
      }
    }
  }

#ifdef WINDOWS
  /* Add available drive letters so users can switch drives */
  {
    DWORD drives = GetLogicalDrives();
    int d;
    for (d = 0; d < 26; d++) {
      if (drives & (1 << d)) {
        char drivename[8];
        DirEntry *de;
        snprintf(drivename, sizeof(drivename), "%c:\\", 'A' + d);
        /* Skip if this is already the current drive */
        if (path[0] && ((path[0] == 'A' + d) || (path[0] == 'a' + d)) && path[1] == ':')
          continue;
        de = malloc(sizeof(*de));
        if (de) {
          de->prev = entry;
          de->next = NULL;
          if (entry) entry->next = de;
          if ((de->name = malloc(8))) {
            snprintf(de->name, 8, "[%s]", drivename);
          }
          memset(de->rawname, 0xa0, 16);
          de->rawname[0] = 'A' + d;
          de->type = T_DIR;
          de->closed = 1;
          de->locked = 0;
          de->track = 0;
          de->sector = 0;
          de->size = 0;
          de->tagged = 0;
          de->mtime = 0;
          entry = de;
          dir->numentries++;
        }
      }
    }
  }
#endif

  while ((dirent = readdir(dirhandle))) {
    namelen = strlen(dirent->d_name);
    if (dirent->d_name[0] == '.') {
      /* skip hidden files and . / .. */
    } else {
      if (dir->numentries) {
	if ((entry->next = malloc(sizeof(*(entry->next)))) == NULL) {
	  goto ReadDirDone;
	}
	entry->next->prev = entry;
	entry = entry->next;
      } else {
	if ((dir->firstentry = malloc(sizeof(*(dir->firstentry)))) == NULL) {
	  goto ReadDirDone;
	}
	entry = dir->firstentry;
	entry->prev = NULL;
      }
      entry->next = NULL;
      if ((entry->name = malloc(namelen + 1))) {
	memcpy(entry->name, dirent->d_name, namelen + 1);
      }
      memset(entry->rawname, 0xa0, sizeof(entry->rawname));
#ifdef WINDOWS
      /* Skip entries whose name would overflow namebuf. */
      if ((name - namebuf) + (int)namelen + 1 > (int)sizeof(namebuf)) {
        continue;
      }
      memcpy(name, entry->name, namelen + 1);
      if ((d = opendir(name))) {
	entry->type = T_DIR;
	closedir(d);
      } else {
	entry->type = T_PRG;
      }
#else
      entry->type = dirent->d_type == DT_DIR ? T_DIR : T_PRG;
      if (dirent->d_type != DT_DIR && dirent->d_type != DT_REG) {
        /* symlink, or a filesystem that reports DT_UNKNOWN (NFS, FUSE,
         * sshfs, overlayfs): ask stat() what it really is, otherwise a
         * symlinked directory shows up as a file and can't be entered */
        struct stat st;
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->name ? entry->name : "");
        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
          entry->type = T_DIR;
        }
      }
#endif
      if (entry->type == T_PRG && entry->name && path_is_disk_image(entry->name)) {
	entry->type = T_DIR;
      }
      entry->closed = 1;
      entry->locked = 0;
      entry->track = 0;
      entry->sector = 0;
      entry->size = 0;
      entry->tagged = 0;
      /* Get modification time */
      {
        struct stat st;
        char fullpath[512];
#ifdef WINDOWS
        snprintf(fullpath, sizeof(fullpath), "%s\\%s", path, entry->name);
#else
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->name);
#endif
        if (stat(fullpath, &st) == 0) {
          entry->mtime = (long)st.st_mtime;
          if (entry->type != T_DIR) {
            entry->size = (unsigned int)st.st_size;
          }
        } else {
          entry->mtime = 0;
        }
      }
      ++(dir->numentries);
    }
  }

 ReadDirDone:
  /* Sort: directories first, then alphabetically */
  if (dir && dir->numentries > 1) {
    /* qsort over an array of entry pointers, then relink: the old bubble
     * sort over the linked list (O(n^2) strcasecmp) stalled the selector
     * for seconds in download folders with thousands of files, on every
     * keypress that re-read the directory. Pseudo entries ("[ ... ]",
     * "<- Back") stay at the head in their original order. */
    DirEntry **arr;
    DirEntry *e, *head = NULL, *tail = NULL;
    int n = 0, i;

    for (e = dir->firstentry; e; e = e->next) n++;
    arr = malloc(sizeof(*arr) * n);
    if (arr) {
      int nsort = 0;
      for (e = dir->firstentry; e; e = e->next) {
        if (e->name && (e->name[0] == '[' || e->name[0] == '<')) {
          if (tail) tail->next = e; else head = e;
          e->prev = tail;
          tail = e;
        } else {
          arr[nsort++] = e;
        }
      }
      qsort(arr, nsort, sizeof(*arr), dir_entry_compare);
      for (i = 0; i < nsort; i++) {
        if (tail) tail->next = arr[i]; else head = arr[i];
        arr[i]->prev = tail;
        tail = arr[i];
      }
      if (tail) tail->next = NULL;
      dir->firstentry = head;
      free(arr);
    }
  }

  return(dir);
}


Dir *dir_read(const char *path) {
  DIR *dirhandle;
  DiskImage *di;
  Dir *dir;

  if ((dirhandle = opendir(path))) {
    dir = dir_read_opendir(dirhandle, path);
    closedir(dirhandle);
  } else if ((di = di_load_image((char *)path))) {
    dir = dir_read_image(di);
    di_free_image(di);
  } else {
    return(NULL);
  }
  return(dir);
}


/* free directory mem */
void dir_free(Dir *dir) {
  DirEntry *entry;
  DirEntry *next;

  entry = dir->firstentry;
  while (entry) {
    if (entry->name) {
      free(entry->name);
    }
    next = entry->next;
    free(entry);
    entry = next;
  }
  if (dir->title) {
    free(dir->title);
  }
  free(dir);
}


DirEntry *dir_find(Dir *dir, int entrynum) {
  DirEntry *de;
  if (!dir || entrynum < 0) return NULL;
  de = dir->firstentry;
  while (de && entrynum--) {
    de = de->next;
  }
  return(de);
}
