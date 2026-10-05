typedef struct direntry {
  struct direntry *next;
  struct direntry *prev;
  char *name;
  unsigned char rawname[16];
  unsigned int size;
  int type;
  int closed;
  int locked;
  int track;
  int sector;
  int tagged;
  long mtime;
} DirEntry;

typedef struct dir {
  int numentries;
  char *title;
  DirEntry *firstentry;
  int blocksfree;  /* -1 if not a disk image */
  /* disk image only (empty strings for a folder): the 2-character disk ID
   * and DOS type from the header, as shown on the C64 directory header line
   * 0 "DISK NAME       " ID 2A. Raw PETSCII bytes, NUL terminated. */
  unsigned char id[3];
  unsigned char dostype[3];
} Dir;

extern char *dir_type[];

/* Names of the pseudo entries the directory reader inserts; the UI must
 * compare against these, not against stale copies of the text. */
#define FS_SELECT_ENTRY "[ SELECT THIS PATH ]"
#define FS_HOME_ENTRY   "[ Go to Home ]"
#define FS_BACK_ENTRY   "<- Back"

/* 1 if the path ends in .d64/.d71/.d81 (any case) */
int path_is_disk_image(const char *path);


Dir *dir_read(const char *path);
void dir_free(Dir *dir);
DirEntry *dir_find(Dir *dir, int entrynum);
