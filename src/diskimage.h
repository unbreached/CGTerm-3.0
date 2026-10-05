typedef enum imagetype {
  D64 = 1,
  D71,
  D81
} ImageType;

typedef enum filetype {
  T_DEL = 0,
  T_SEQ,
  T_PRG,
  T_USR,
  T_REL,
  T_CBM,
  T_DIR
} FileType;

typedef struct ts {
  unsigned char track;
  unsigned char sector;
} TrackSector;

typedef struct diskimage {
  char *filename;
  int size;
  ImageType type;
  unsigned char *image;
  TrackSector bam;
  TrackSector bam2;
  TrackSector dir;
  int openfiles;
  int blocksfree;
  int modified;
  int status;
  TrackSector statusts;
} DiskImage;

typedef struct rawdirentry {
  TrackSector nextts;
  unsigned char type;
  TrackSector startts;
  unsigned char rawname[16];
  TrackSector relsidets;
  unsigned char relrecsize;
  unsigned char unused[4];
  TrackSector replacetemp;
  unsigned char sizelo;
  unsigned char sizehi;
} RawDirEntry;

typedef struct imagefile {
  DiskImage *diskimage;
  RawDirEntry *rawdirentry;
  char mode;
  int position;
  TrackSector ts;
  TrackSector nextts;
  unsigned char *buffer;
  int bufptr;
  int buflen;
} ImageFile;


DiskImage *di_load_image(char *name);
DiskImage *di_create_image(char *name, int size);
int di_free_image(DiskImage *di);   /* 0 ok, -1 if the image could not be written */
int di_sync(DiskImage *di);
int di_blocks(ImageType type);      /* data blocks by geometry (683 / 1366 / 3200) */

int di_status(DiskImage *di, char *status);

ImageFile *di_open(DiskImage *di, const unsigned char *rawname, FileType type, const char *mode);
void di_close(ImageFile *imgfile);
int di_read(ImageFile *imgfile, unsigned char *buffer, int len);
int di_write(ImageFile *imgfile, unsigned char *buffer, int len);

int di_format(DiskImage *di, const unsigned char *rawname, const unsigned char *rawid);
int di_delete(DiskImage *di, const unsigned char *rawpattern, FileType type);
int di_rename(DiskImage *di, const unsigned char *oldrawname, const unsigned char *newrawname, FileType type);
int di_delete_exact(DiskImage *di, const unsigned char *rawname);
int di_rename_exact(DiskImage *di, const unsigned char *oldrawname, const unsigned char *newrawname);
ImageFile *di_open_exact(DiskImage *di, const unsigned char *rawname);

/* Disk tools (file selector V / L / I keys). All take exact 16-byte
 * 0xA0-padded PETSCII names; none of them write the image file, the caller
 * flushes with di_free_image() / di_sync(). */

/* Rebuild the BAM from the directory like CBM DOS VALIDATE. fixed_blocks
 * gets (blocks free after) - (blocks free before), removed_entries the
 * number of unclosed (splat) entries dropped. Either may be NULL. 0 ok. */
int di_validate(DiskImage *di, int *fixed_blocks, int *removed_entries);
/* set (locked != 0) or clear the 0x40 lock bit; 0 ok, 62 not found */
int di_set_locked(DiskImage *di, const unsigned char *rawname, int locked);
/* rewrite the header name (16 bytes, 0xA0 padded) and/or 2-byte ID; pass
 * NULL to leave one of them unchanged. 0 ok. */
int di_rename_disk(DiskImage *di, const unsigned char *rawname16, const unsigned char *rawid2);
/* type (T_xxx), size in blocks, closed / locked flags of an entry; any
 * output may be NULL. 0 ok, 62 not found. */
int di_entry_info(DiskImage *di, const unsigned char *rawname, int *type, int *blocks, int *closed, int *locked);
/* 2-byte disk ID / 2-byte DOS type ("2A", "3D") in the header, not NUL
 * terminated */
unsigned char *di_id(DiskImage *di);
unsigned char *di_dostype(DiskImage *di);

int di_sectors_per_track(ImageType type, int track);
int di_tracks(ImageType type);

unsigned char *di_title(DiskImage *di);
int di_track_blocks_free(DiskImage *di, int track);
int di_is_ts_free(DiskImage *di, TrackSector ts);
void di_alloc_ts(DiskImage *di, TrackSector ts);
void di_free_ts(DiskImage *di, TrackSector ts);

int di_rawname_from_name(unsigned char *rawname, char *name);
int di_name_from_rawname(char *name, unsigned char *rawname);


unsigned char *di_name_to_rawname(char *name);