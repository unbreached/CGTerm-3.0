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


char *dir_type[] = {"DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR"};


/* convert A0 padded petscii name to ascii */
char *make_name(unsigned char *rawname) {
  int l;
  char *name;

  for (l = 0; rawname[l] != 0xa0 && l < 16; ++l);
  if ((name = malloc(l+1)) == NULL) {
    return(NULL);
  }
  memcpy(name, rawname, l);
  name[l] = 0;
  for (l = 0; rawname[l]; ++l) {

  }
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
      if ((use_entry->name = malloc(20))) {
        strcpy(use_entry->name, "[ SELECT THIS PATH ]");
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
	entry->name = make_name(buffer + offset + 5);
	memcpy(entry->rawname, buffer + offset + 5, 16);
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


Dir *dir_read_opendir(DIR *dirhandle, const char *path) {
  Dir *dir;
  DirEntry *entry = NULL;
  struct dirent *dirent;
  unsigned namelen;
  char *p;
#ifdef WINDOWS
  DIR *d;
  char namebuf[256];
  char *name;
  int len;

  /* Copy path to namebuf, add a slash, and remember where it ends */
  len = strlen(path);
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

  if ((dir->title = malloc(strlen(path) + 1))) {
    strcpy(dir->title, path);
  }

  /* Add "[ Use this folder ]" as first entry */
  {
    DirEntry *use_entry;
    if ((use_entry = malloc(sizeof(*use_entry))) != NULL) {
      use_entry->prev = NULL;
      use_entry->next = NULL;
      if ((use_entry->name = malloc(20))) {
        strcpy(use_entry->name, "[ SELECT THIS PATH ]");
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
      memcpy(name, entry->name, namelen + 1);
      if ((d = opendir(name))) {
	entry->type = T_DIR;
	closedir(d);
      } else {
	entry->type = T_PRG;
      }
#else
      entry->type = dirent->d_type == DT_DIR ? T_DIR : T_PRG;
#endif
      if (entry->type == T_PRG) {
	if ((p = strrchr(entry->name, '.'))) {
	  if (strlen(p) == 4) {
	    if (p[1] == 'd' || p[1] == 'D') {
	      if (isdigit(p[2]) && isdigit(p[3])) {
		entry->type = T_DIR;
	      }
	    }
	  }
	}
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
    int swapped;
    do {
      DirEntry *a;
      swapped = 0;
      a = dir->firstentry;
      while (a && a->next) {
        DirEntry *b = a->next;
        int doswap = 0;
        /* Never sort special entries ([ Use this folder ], <- Back) */
        if ((a->name && (a->name[0] == '[' || a->name[0] == '<')) ||
            (b->name && (b->name[0] == '[' || b->name[0] == '<'))) {
          a = a->next;
          continue;
        }
        /* Sort priority: 0=real dir, 1=disk image, 2=file */
        {
          int pa = 2, pb = 2;
          if (a->type == T_DIR) {
            char *d = a->name ? strrchr(a->name, '.') : NULL;
            pa = (d && strlen(d)==4 && (d[1]=='d'||d[1]=='D') && isdigit(d[2]) && isdigit(d[3])) ? 1 : 0;
          }
          if (b->type == T_DIR) {
            char *d = b->name ? strrchr(b->name, '.') : NULL;
            pb = (d && strlen(d)==4 && (d[1]=='d'||d[1]=='D') && isdigit(d[2]) && isdigit(d[3])) ? 1 : 0;
          }
          if (pa != pb) {
            doswap = (pa > pb);
          } else if (pa == 0) {
            /* Both real dirs: alphabetical */
            if (a->name && b->name)
              doswap = (strcasecmp(a->name, b->name) > 0);
          } else if (pa == 1) {
            /* Both disk images: alphabetical */
            if (a->name && b->name)
              doswap = (strcasecmp(a->name, b->name) > 0);
          } else {
            /* Both files: newest first (by mtime) */
            doswap = (a->mtime < b->mtime);
          }
        }
        if (doswap) {
          /* Swap data fields */
          char *tn = a->name; a->name = b->name; b->name = tn;
          unsigned char tr[16];
          memcpy(tr, a->rawname, 16); memcpy(a->rawname, b->rawname, 16); memcpy(b->rawname, tr, 16);
          unsigned int ts = a->size; a->size = b->size; b->size = ts;
          int tt = a->type; a->type = b->type; b->type = tt;
          tt = a->closed; a->closed = b->closed; b->closed = tt;
          tt = a->locked; a->locked = b->locked; b->locked = tt;
          tt = a->track; a->track = b->track; b->track = tt;
          tt = a->sector; a->sector = b->sector; b->sector = tt;
          tt = a->tagged; a->tagged = b->tagged; b->tagged = tt;
          { long tm = a->mtime; a->mtime = b->mtime; b->mtime = tm; }
          swapped = 1;
        }
        a = a->next;
      }
    } while (swapped);
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
  de = dir->firstentry;
  while (entrynum--) {
    de = de->next;
  }
  return(de);
}
