#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskimage.h"


typedef struct errormessage {
  signed int number;
  char *string;
} ErrorMessage;


ErrorMessage error_msg[] = {
  /* non-errors */
  { 0, "ok" },
  { 1, "files scratched" },
  { 2, "partition selected" },
  /* errors */
  { 20, "read error (block header not found)" },
  { 21, "read error (drive not ready)" },
  { 22, "read error (data block not found)" },
  { 23, "read error (crc error in data block)" },
  { 24, "read error (byte sector header)" },
  { 25, "write error (write-verify error)" },
  { 26, "write protect on" },
  { 27, "read error (crc error in header)" },
  { 30, "syntax error (general syntax)" },
  { 31, "syntax error (invalid command)" },
  { 32, "syntax error (long line)" },
  { 33, "syntax error (invalid file name)" },
  { 34, "syntax error (no file given)" },
  { 39, "syntax error (invalid command)" },
  { 50, "record not present" },
  { 51, "overflow in record" },
  { 52, "file too large" },
  { 60, "write file open" },
  { 61, "file not open" },
  { 62, "file not found" },
  { 63, "file exists" },
  { 64, "file type mismatch" },
  { 65, "no block" },
  { 66, "illegal track and sector" },
  { 67, "illegal system t or s" },
  { 70, "no channel" },
  { 71, "directory error" },
  { 72, "disk full" },
  { 73, "dos mismatch" },
  { 74, "drive not ready" },
  { 75, "format error" },
  { 76, "controller error" },
  { 77, "selected partition illegal" },
  { -1, NULL }
};


/* convert to rawname (ASCII to PETSCII: lowercase→uppercase) */
int di_rawname_from_name(unsigned char *rawname, char *name) {
  int i;

  memset(rawname, 0xa0, 16);
  for (i = 0; i < 16 && name[i]; ++i) {
    unsigned char c = (unsigned char)name[i];
    /* Convert lowercase ASCII to uppercase PETSCII */
    if (c >= 'a' && c <= 'z') c -= 32;
    rawname[i] = c;
  }
  return(i);
}


/* convert from rawname */
int di_name_from_rawname(char *name, unsigned char *rawname) {
  int i;

  for (i = 0; i < 16 && rawname[i] != 0xa0; ++i) {
    name[i] = rawname[i];
  }
  name[i] = 0;
  return(i);
}


/* return status string */
int di_status(DiskImage *di, char *status) {
  ErrorMessage *err = error_msg;

  /* special case for power up */
  if (di->status == 254) {
    switch (di->type) {
    case D64:
      snprintf(status, 80, "73,cbm dos v2.6 1541,00,00");
      break;
    case D71:
      snprintf(status, 80, "73,cbm dos v3.0 1571,00,00");
      break;
    case D81:
      snprintf(status, 80, "73,copyright cbm dos v10 1581,00,00");
      break;
    }
    return(73);
  }

  while (err->number >= 0) {
    if (di->status == err->number) {
      snprintf(status, 80, "%02d,%s,%02d,%02d", di->status, err->string, di->statusts.track, di->statusts.sector);
      return(di->status);
    }
    ++err;
  }
  snprintf(status, 80, "%02d,unknown error,%02d,%02d", di->status, di->statusts.track, di->statusts.sector);
  return(di->status);
}


int set_status(DiskImage *di, int status, int track, int sector) {
  di->status = status;
  di->statusts.track = track;
  di->statusts.sector = sector;
  return(status);
}


/* return write interleave */
int interleave(ImageType type) {
  switch (type) {
  case D64:
    return(10);
    break;
  case D71:
    return(6);
    break;
  default:
    return(1);
    break;
  }
}


/* return number of tracks for image type */
/* number of data blocks by geometry (excludes any trailing error table) */
int di_blocks(ImageType type) {
  switch (type) {
  case D64: return(683);
  case D71: return(1366);
  case D81: return(3200);
  default:  return(0);
  }
}


int di_tracks(ImageType type) {
  switch (type) {
  case D64:
    return(35);
    break;
  case D71:
    return(70);
    break;
  case D81:
    return(80);
    break;
  }
  return(0);
}


/* return disk geometry for track */
int di_sectors_per_track(ImageType type, int track) {
  switch (type) {
  case D71:
    if (track > 35) {
      track -= 35;
    }
    // fall through
  case D64:
    if (track < 18) {
      return(21);
    } else if (track < 25) {
      return(19);
    } else if (track < 31) {
      return(18);
    } else {
      return(17);
    }
    break;
  case D81:
    return(40);
    break;
  }
  return(0);
}

/* convert track, sector to blocknum */
int get_block_num(ImageType type, TrackSector ts) {
  int block;

  switch (type) {
  case D64:
    if (ts.track < 18) {
      block = (ts.track - 1) * 21;
    } else if (ts.track < 25) {
      block = (ts.track - 18) * 19 + 17 * 21;
    } else if (ts.track < 31) {
      block = (ts.track - 25) * 18 + 17 * 21 + 7 * 19;
    } else {
      block = (ts.track - 31) * 17 + 17 * 21 + 7 * 19 + 6 * 18;
    }
    return(block + ts.sector);
    break;
  case D71:
    if (ts.track > 35) {
      block = 683;
      ts.track -= 35;
    } else {
      block = 0;
    }
    if (ts.track < 18) {
      block += (ts.track - 1) * 21;
    } else if (ts.track < 25) {
      block += (ts.track - 18) * 19 + 17 * 21;
    } else if (ts.track < 31) {
      block += (ts.track - 25) * 18 + 17 * 21 + 7 * 19;
    } else {
      block += (ts.track - 31) * 17 + 17 * 21 + 7 * 19 + 6 * 18;
    }
    return(block + ts.sector);
    break;
  case D81:
    return((ts.track - 1) * 40 + ts.sector);
    break;
  }
  return(0);
}


/* validate a track/sector against the image geometry.
   track/sector come straight from untrusted image data, so every chain
   link must be checked before it is used to index the image buffer. */
int di_ts_valid(DiskImage *di, TrackSector ts) {
  int blk;

  if (ts.track < 1 || ts.track > di_tracks(di->type)) {
    return(0);
  }
  if (ts.sector >= di_sectors_per_track(di->type, ts.track)) {
    return(0);
  }
  blk = get_block_num(di->type, ts);
  if (blk < 0 || blk >= di_blocks(di->type)) {
    return(0);
  }
  return(1);
}


/* get a pointer to block data.  Clamps the computed block into the image
   so a malformed/hostile track/sector can never produce an out-of-bounds
   pointer; callers that pass attacker-controlled ts should also gate on
   di_ts_valid(), but this is the last line of defence for every caller. */
unsigned char *get_ts_addr(DiskImage *di, TrackSector ts) {
  int blk = get_block_num(di->type, ts);
  int maxblk = di_blocks(di->type);

  if (blk < 0) {
    blk = 0;
  } else if (blk >= maxblk) {
    blk = maxblk - 1;
  }
  return(di->image + blk * 256);
}


/* return a pointer to the next block in the chain.  A link that does not
   point at a valid block terminates the chain (track 0) so loops that walk
   *->track cannot follow attacker-crafted pointers off the image. */
TrackSector next_ts_in_chain(DiskImage *di, TrackSector ts) {
  unsigned char *p;
  TrackSector newts;

  p = get_ts_addr(di, ts);
  newts.track = p[0];
  newts.sector = p[1];
  if (newts.track != 0 && !di_ts_valid(di, newts)) {
    newts.track = 0;
    newts.sector = 0;
  }
  return(newts);
}


/* return a pointer to the disk title */
unsigned char *di_title(DiskImage *di) {
  switch (di->type) {
  default:
  case D64:
  case D71:
    return(get_ts_addr(di, di->dir) + 144);
    break;
  case D81:
    return(get_ts_addr(di, di->dir) + 4);
    break;
  }
}


/* return number of free blocks in track */
int di_track_blocks_free(DiskImage *di, int track) {
  unsigned char *bam;

  switch (di->type) {
  default:
  case D64:
    bam = get_ts_addr(di, di->bam);
    break;
  case D71:
    bam = get_ts_addr(di, di->bam);
    if (track >= 36) {
      return(bam[track + 185]);
    }
    break;
  case D81:
    if (track <= 40) {
      bam = get_ts_addr(di, di->bam);
    } else {
      bam = get_ts_addr(di, di->bam2);
      track -= 40;
    }
    return(bam[track * 6 + 10]);
    break;
  }
  return(bam[track * 4]);
}


/* count number of free blocks */
int blocks_free(DiskImage *di) {
  int track;
  int blocks = 0;

  for (track = 1; track <= di_tracks(di->type); ++track) {
    if (track != di->dir.track) {
      blocks += di_track_blocks_free(di, track);
    }
  }
  return(blocks);
}


/* check if track, sector is free in BAM */
int di_is_ts_free(DiskImage *di, TrackSector ts) {
  unsigned char mask;
  unsigned char *bam;

  switch (di->type) {
  case D64:
    bam = get_ts_addr(di, di->bam);
    if (bam[ts.track * 4]) {
      mask = 1<<(ts.sector & 7);
      return(bam[ts.track * 4 + ts.sector / 8 + 1] & mask ? 1 : 0);
    } else {
      return(0);
    }
    break;
  case D71:
    mask = 1<<(ts.sector & 7);
    if (ts.track < 36) {
      bam = get_ts_addr(di, di->bam);
      return(bam[ts.track * 4 + ts.sector / 8 + 1] & mask ? 1 : 0);
    } else {
      bam = get_ts_addr(di, di->bam2);
      return(bam[(ts.track - 35) * 3 + ts.sector / 8 - 3] & mask ? 1 : 0);
    }
    break;
  case D81:
    mask = 1<<(ts.sector & 7);
    if (ts.track < 41) {
      bam = get_ts_addr(di, di->bam);
    } else {
      bam = get_ts_addr(di, di->bam2);
      ts.track -= 40;
    }
    return(bam[ts.track * 6 + ts.sector / 8 + 11] & mask ? 1 : 0);
    break;
  }
  return(0);
}


/* allocate track, sector in BAM */
void di_alloc_ts(DiskImage *di, TrackSector ts) {
  unsigned char mask;
  unsigned char *bam;

  di->modified = 1;
  switch (di->type) {
  case D64:
    bam = get_ts_addr(di, di->bam);
    bam[ts.track * 4] -= 1;
    mask = 1<<(ts.sector & 7);
    bam[ts.track * 4 + ts.sector / 8 + 1] &= ~mask;
    break;
  case D71:
    mask = 1<<(ts.sector & 7);
    if (ts.track < 36) {
      bam = get_ts_addr(di, di->bam);
      bam[ts.track * 4] -= 1;
      bam[ts.track * 4 + ts.sector / 8 + 1] &= ~mask;
    } else {
      bam = get_ts_addr(di, di->bam);
      bam[ts.track + 185] -= 1;
      bam = get_ts_addr(di, di->bam2);
      bam[(ts.track - 35) * 3 + ts.sector / 8 - 3] &= ~mask;
    }
    break;
  case D81:
    if (ts.track < 41) {
      bam = get_ts_addr(di, di->bam);
    } else {
      bam = get_ts_addr(di, di->bam2);
      ts.track -= 40;
    }
    bam[ts.track * 6 + 10] -= 1;
    mask = 1<<(ts.sector & 7);
    bam[ts.track * 6 + ts.sector / 8 + 11] &= ~mask;
    break;
  }
}


/* allocate next available block */
TrackSector alloc_next_ts(DiskImage *di, TrackSector prevts) {
  unsigned char *bam;
  int spt, s1, s2, t1, t2, bpt, boff, i;
  TrackSector ts;

  switch (di->type) {
  default:
  case D64:
    s1 = 1;
    t1 = 35;
    s2 = 1;
    t2 = 35;
    bpt = 4;
    boff = 0;
    break;
  case D71:
    s1 = 1;
    t1 = 35;
    s2 = 36;
    t2 = 70;
    bpt = 4;
    boff = 0;
    break;
  case D81:
    s1 = 1;
    t1 = 40;
    s2 = 41;
    t2 = 80;
    bpt = 6;
    boff = 10;
    break;
  }

  (void)bam; (void)bpt; (void)boff;
  /* Gate each track on di_track_blocks_free(), which knows where every
   * image type keeps its per-track free count. Indexing the BAM sector
   * directly was wrong for the D71 second side (its counts live in 18/0,
   * not 53/0), which made tracks 62-70 unreachable and reported "disk
   * full" with ~170 blocks free. CBM DOS never puts file data on the
   * directory track, so skip it: blocks_free() excludes it from the total
   * and the directory must be able to grow. */
  for (ts.track = s1; ts.track <= t1; ++ts.track) {
    if (ts.track == di->dir.track) {
      continue;
    }
    if (di_track_blocks_free(di, ts.track)) {
      spt = di_sectors_per_track(di->type, ts.track);
      ts.sector = (prevts.sector + interleave(di->type)) % spt;
      /* bound the scan: a crafted BAM can claim free blocks (count byte)
       * while marking every sector allocated, which would spin forever */
      for (i = 0; i < spt; ++i, ts.sector = (ts.sector + 1) % spt) {
	if (di_is_ts_free(di, ts)) {
	  di_alloc_ts(di, ts);
	  return(ts);
	}
      }
    }
  }

  if (di->type == D71 || di->type == D81) {
    for (ts.track = s2; ts.track <= t2; ++ts.track) {
      if (ts.track == di->dir.track || (di->type == D71 && ts.track == 53)) {
	continue;
      }
      if (di_track_blocks_free(di, ts.track)) {
	spt = di_sectors_per_track(di->type, ts.track);
	ts.sector = (prevts.sector + interleave(di->type)) % spt;
	for (i = 0; i < spt; ++i, ts.sector = (ts.sector + 1) % spt) {
	  if (di_is_ts_free(di, ts)) {
	    di_alloc_ts(di, ts);
	    return(ts);
	  }
	}
      }
    }
  }

  ts.track = 0;
  ts.sector = 0;
  return(ts);
}


/* allocate next available directory block */
TrackSector alloc_next_dir_ts(DiskImage *di) {
  unsigned char *p;
  int spt, hops;
  TrackSector ts, lastts;

  if (di_track_blocks_free(di, di->bam.track)) {
    ts.track = di->bam.track;
    ts.sector = 0;
    lastts = ts;
    for (hops = 0; ts.track && hops < di->size / 256; hops++) {
      lastts = ts;
      ts = next_ts_in_chain(di, ts);
    }
    ts.track = lastts.track;
    spt = di_sectors_per_track(di->type, ts.track);
    if (spt <= 0) {
      ts.track = 0;
      ts.sector = 0;
      return(ts);
    }
    ts.sector = (lastts.sector + 3) % spt;
    for (hops = 0; hops < di->size / 256; ts.sector = (ts.sector + 1) % spt, hops++) {
      if (di_is_ts_free(di, ts)) {
	di_alloc_ts(di, ts);
	p = get_ts_addr(di, lastts);
	p[0] = ts.track;
	p[1] = ts.sector;
	p = get_ts_addr(di, ts);
	memset(p, 0, 256);
	p[1] = 0xff;
	di->modified = 1;
	return(ts);
      }
    }
    /* no free sector found (corrupt or full image) */
    ts.track = 0;
    ts.sector = 0;
    return(ts);
  } else {
    ts.track = 0;
    ts.sector = 0;
    return(ts);
  }
}


/* free a block in the BAM */
void di_free_ts(DiskImage *di, TrackSector ts) {
  unsigned char mask;
  unsigned char *bam;

  if (di_is_ts_free(di, ts)) {
    /* already free (looped or overlapping chain): freeing it again would
     * wrap the per-track count byte past the number of sectors */
    return;
  }
  di->modified = 1;
  switch (di->type) {
  case D64:
    mask = 1<<(ts.sector & 7);
    bam = get_ts_addr(di, di->bam);
    bam[ts.track * 4 + ts.sector / 8 + 1] |= mask;
    bam[ts.track * 4] += 1;
    break;
  case D71:
    mask = 1<<(ts.sector & 7);
    if (ts.track < 36) {
      bam = get_ts_addr(di, di->bam);
      bam[ts.track * 4 + ts.sector / 8 + 1] |= mask;
      bam[ts.track * 4] += 1;
    } else {
      bam = get_ts_addr(di, di->bam);
      bam[ts.track + 185] += 1;
      bam = get_ts_addr(di, di->bam2);
      bam[(ts.track - 35) * 3 + ts.sector / 8 - 3] |= mask;
    }
    break;
  case D81:
    if (ts.track < 41) {
      bam = get_ts_addr(di, di->bam);
    } else {
      bam = get_ts_addr(di, di->bam2);
      ts.track -= 40;
    }
    mask = 1<<(ts.sector & 7);
    bam[ts.track * 6 + ts.sector / 8 + 11] |= mask;
    bam[ts.track * 6 + 10] += 1;
    break;
  default:
    break;
  }
}


/* free a chain of blocks */
void free_chain(DiskImage *di, TrackSector ts) {
  int hops;
  /* validate the first link too — di_delete() passes a raw, possibly
   * hostile directory startts straight in (next_ts_in_chain validates
   * every subsequent link, but the first was unchecked) */
  for (hops = 0; ts.track && di_ts_valid(di, ts) && hops < di_blocks(di->type); hops++) {
    if (di_is_ts_free(di, ts)) {
      break;   /* chain loops back into freed blocks, or runs into free space */
    }
    di_free_ts(di, ts);
    ts = next_ts_in_chain(di, ts);
  }
}


DiskImage *di_load_image(char *name) {
  FILE *file;
  int filesize, l, read;
  DiskImage *di;

  /* open image */
  if ((file = fopen(name, "rb")) == NULL) {
    //puts("fopen failed");
    return(NULL);
  }

  /* get file size*/
  if (fseek(file, 0, SEEK_END)) {
    //puts("fseek failed");
    fclose(file);
    return(NULL);
  }
  filesize = ftell(file);
  fseek(file, 0, SEEK_SET);

  if ((di = malloc(sizeof(*di))) == NULL) {
    //puts("malloc failed");
    fclose(file);
    return(NULL);
  }

  /* check image type. Images with a trailing per-block error-info table
   * (one byte per block: 175531 / 351062 / 822400) are very common in
   * online archives; keep the full file length in di->size so di_sync()
   * preserves the table, and address blocks by geometry (di_blocks). */
  switch (filesize) {
  case 174848: // standard D64
  case 174848 + 683:
    di->type = D64;
    di->bam.track = 18;
    di->bam.sector = 0;
    di->dir = di->bam;
    break;
  case 349696:
  case 349696 + 1366:
    di->type = D71;
    di->bam.track = 18;
    di->bam.sector = 0;
    di->bam2.track = 53;
    di->bam2.sector = 0;
    di->dir = di->bam;
    break;
  case 819200:
  case 819200 + 3200:
    di->type = D81;
    di->bam.track = 40;
    di->bam.sector = 1;
    di->bam2.track = 40;
    di->bam2.sector = 2;
    di->dir.track = 40;
    di->dir.sector = 0;
    break;
  default:
    //puts("unknown type");
    free(di);
    fclose(file);
    return(NULL);
  }

  di->size = filesize;

  /* allocate buffer for image */
  if ((di->image = malloc(filesize)) == NULL) {
    //puts("image malloc failed");
    free(di);
    fclose(file);
    return(NULL);
  }

  /* read file into buffer */
  read = 0;
  while (read < filesize) {
    if ((l = fread(di->image + read, 1, filesize - read, file))) {
      read += l;
    } else {
      //puts("fread failed");
      free(di->image);
      free(di);
      fclose(file);
      return(NULL);
    }
  }

  di->filename = malloc(strlen(name) + 1);
  if (di->filename) {
    memcpy(di->filename, name, strlen(name) + 1);
  }
  di->openfiles = 0;
  di->blocksfree = blocks_free(di);
  di->modified = 0;
  set_status(di, 254, 0, 0);
  return(di);
}


DiskImage *di_create_image(char *name, int size) {
  DiskImage *di;

  if ((di = malloc(sizeof(*di))) == NULL) {
    //puts("malloc failed");
    return(NULL);
  }

  /* check image type */
  switch (size) {
  case 174848:
    di->type = D64;
    di->bam.track = 18;
    di->bam.sector = 0;
    di->dir = di->bam;
    break;
  case 349696:
    di->type = D71;
    di->bam.track = 18;
    di->bam.sector = 0;
    di->bam2.track = 53;
    di->bam2.sector = 0;
    di->dir = di->bam;
    break;
  case 819200:
    di->type = D81;
    di->bam.track = 40;
    di->bam.sector = 1;
    di->bam2.track = 40;
    di->bam2.sector = 2;
    di->dir.track = 40;
    di->dir.sector = 0;
    break;
  default:
    //puts("unknown type");
    free(di);
    return(NULL);
  }

  di->size = size;

  /* allocate buffer for image */
  if ((di->image = malloc(size)) == NULL) {
    //puts("image malloc failed");
    free(di);
    return(NULL);
  }
  memset(di->image, 0, size);

  di->filename = malloc(strlen(name) + 1);
  if (di->filename) {
    memcpy(di->filename, name, strlen(name) + 1);
  }
  di->openfiles = 0;
  di->blocksfree = blocks_free(di);
  di->modified = 1;
  set_status(di, 254, 0, 0);
  return(di);
}


int di_sync(DiskImage *di) {
  FILE *file;
  int l, left;
  unsigned char *image;
  char *tmpname;
  size_t namelen;

  if (di->filename == NULL) return(-1);

  /* Write to a sibling temp file and only rename it over the original on full
   * success. Opening the original "wb" truncates it to zero immediately, so a
   * mid-write failure (disk full, media removed) would otherwise destroy the
   * user's existing .d64/.d71/.d81 with no recovery. */
  namelen = strlen(di->filename);
  if ((tmpname = malloc(namelen + 5)) == NULL) return(-1);
  memcpy(tmpname, di->filename, namelen);
  memcpy(tmpname + namelen, ".tmp", 5);   /* copies the trailing NUL too */

  if ((file = fopen(tmpname, "wb")) == NULL) {
    free(tmpname);
    return(-1);   /* original left intact */
  }
  image = di->image;
  left = di->size;
  l = 0;
  while (left) {
    if ((l = fwrite(image, 1, left, file)) == 0) {
      fclose(file);
      remove(tmpname);
      free(tmpname);
      return(-1);   /* write failed — original left intact */
    }
    left -= l;
    image += l;
  }
  if (fclose(file) != 0) {
    remove(tmpname);
    free(tmpname);
    return(-1);
  }

  /* Replace the original with the fully-written temp. */
#if defined(WINDOWS) || defined(__WIN32__)
  remove(di->filename);   /* Windows rename() won't overwrite an existing file */
#endif
  if (rename(tmpname, di->filename) != 0) {
    /* fallback for platforms/filesystems where rename can't overwrite */
    remove(di->filename);
    if (rename(tmpname, di->filename) != 0) {
      remove(tmpname);
      free(tmpname);
      return(-1);   /* leave di->modified set so a later close can retry */
    }
  }
  di->modified = 0;
  free(tmpname);
  return(0);
}


/* Flush (if modified) and free. Returns 0 on success, -1 when the image
 * could not be written back - callers must not delete their source data
 * (e.g. a downloaded temp file) before checking this. */
int di_free_image(DiskImage *di) {
  int rc = 0;

  if (di->modified) {
    rc = di_sync(di);
  }
  if (di->filename) {
    free(di->filename);
  }
  free(di->image);
  free(di);
  return(rc);
}


int match_pattern(const unsigned char *rawpattern, const unsigned char *rawname) {
  int i;

  for (i = 0; i < 16; ++i) {
    if (rawpattern[i] == '*') {
      return(1);
    }
    if (rawname[i] == 0xa0) {
      if (rawpattern[i] == 0xa0) {
	return(1);
      } else {
	return(0);
      }
    } else {
      if (rawpattern[i] == '?' || rawpattern[i] == rawname[i]) {
      } else {
	return(0);
      }
    }
  }
  return(1);
}


/* The directory chain hangs off the directory header block (18/0, or 40/0
 * on a D81). Walking it from di->bam only worked on D64/D71 where the two
 * coincide; on a D81 that walked the BAM sectors 40/1 -> 40/2 instead and
 * never reached the real directory. */
RawDirEntry *find_file_entry(DiskImage *di, const unsigned char *rawpattern, FileType type) {
  unsigned char *buffer;
  TrackSector ts;
  RawDirEntry *rde;
  int offset, hops;

  ts = next_ts_in_chain(di, di->dir);
  for (hops = 0; ts.track && hops < di_blocks(di->type); hops++) {
    buffer = get_ts_addr(di, ts);
    for (offset = 0; offset < 256; offset += 32) {
      rde = (RawDirEntry *)(buffer + offset);
      if ((rde->type & ~0x40) == (type | 0x80)) {
	if (match_pattern(rawpattern, rde->rawname)) {
	  return(rde);
	}
      }
    }
    ts = next_ts_in_chain(di, ts);
  }
  return(NULL);
}


/* Exact (byte-for-byte) name match of any non-deleted entry, closed or
 * not. Used by the UI for delete/rename/extract of an entry the user picked
 * from a listing: those names are raw PETSCII and may legitimately contain
 * '*' or '?', which the wildcard matcher would expand to other files. */
RawDirEntry *find_file_entry_exact(DiskImage *di, const unsigned char *rawname) {
  unsigned char *buffer;
  TrackSector ts;
  RawDirEntry *rde;
  int offset, hops;

  ts = next_ts_in_chain(di, di->dir);
  for (hops = 0; ts.track && hops < di_blocks(di->type); hops++) {
    buffer = get_ts_addr(di, ts);
    for (offset = 0; offset < 256; offset += 32) {
      rde = (RawDirEntry *)(buffer + offset);
      if (rde->type != 0 && memcmp(rde->rawname, rawname, 16) == 0) {
	return(rde);
      }
    }
    ts = next_ts_in_chain(di, ts);
  }
  return(NULL);
}


RawDirEntry *alloc_file_entry(DiskImage *di, const unsigned char *rawname, FileType type) {
  unsigned char *buffer;
  TrackSector ts;
  RawDirEntry *rde;
  int offset, hops;

  /* check if file already exists */
  ts = next_ts_in_chain(di, di->dir);
  for (hops = 0; ts.track && hops < di_blocks(di->type); hops++) {
    buffer = get_ts_addr(di, ts);
    for (offset = 0; offset < 256; offset += 32) {
      rde = (RawDirEntry *)(buffer + offset);
      if (rde->type) {
	if (strncmp((const char *)rawname, (const char *)rde->rawname, 16) == 0) {
	  set_status(di, 63, 0, 0);
	  //puts("file exists");
	  return(NULL);
	}
      }
    }
    ts = next_ts_in_chain(di, ts);
  }

  /* allocate empty slot */
  ts = next_ts_in_chain(di, di->dir);
  for (hops = 0; ts.track && hops < di_blocks(di->type); hops++) {
    buffer = get_ts_addr(di, ts);
    for (offset = 0; offset < 256; offset += 32) {
      rde = (RawDirEntry *)(buffer + offset);
      if (rde->type == 0) {
	memset((unsigned char *)rde + 2, 0, 30);
	memcpy(rde->rawname, rawname, 16);
	rde->type = type;
	di->modified = 1;
	return(rde);
      }
    }
    ts = next_ts_in_chain(di, ts);
  }

  /* allocate new dir block */
  ts = alloc_next_dir_ts(di);
  if (ts.track) {
    rde = (RawDirEntry *)get_ts_addr(di, ts);
    memset((unsigned char *)rde + 2, 0, 30);
    memcpy(rde->rawname, rawname, 16);
    rde->type = type;
    di->modified = 1;
    return(rde);
  } else {
    set_status(di, 72, 0, 0);
    //puts("directory full");
    return(NULL);
  }
}


/* open a file */
ImageFile *di_open(DiskImage *di, const unsigned char *rawname, FileType type, const char *mode) {
  ImageFile *imgfile;
  RawDirEntry *rde;
  unsigned char *p;

  set_status(di, 255, 0, 0);

  if (strcmp("rb", mode) == 0) {

    if ((imgfile = malloc(sizeof(*imgfile))) == NULL) {
      //puts("malloc failed");
      return(NULL);
    }
    if (strcmp("$", (const char *)rawname) == 0) {
      imgfile->mode = 'r';
      imgfile->ts = di->dir;
      p = get_ts_addr(di, di->dir);
      imgfile->buffer = p + 2;
      imgfile->nextts.track = p[0];
      imgfile->nextts.sector = p[1];
      imgfile->buflen = 254;
      rde = NULL;
    } else {
      if ((rde = find_file_entry(di, rawname, type)) == NULL) {
	set_status(di, 62, 0, 0);
	//puts("find_file_entry failed");
	free(imgfile);
	return(NULL);
      }
      imgfile->mode = 'r';
      if (!di_ts_valid(di, rde->startts)) {
	/* entry points outside the image (damaged dump, or a 0-block
	 * file): refuse rather than silently read block 1/0 */
	set_status(di, 66, rde->startts.track, rde->startts.sector);
	free(imgfile);
	return(NULL);
      }
      imgfile->ts = rde->startts;
      p = get_ts_addr(di, rde->startts);
      imgfile->buffer = p + 2;
      imgfile->nextts.track = p[0];
      imgfile->nextts.sector = p[1];
      if (imgfile->nextts.track == 0) {
	imgfile->buflen = (imgfile->nextts.sector >= 1) ? imgfile->nextts.sector - 1 : 0;
      } else {
	imgfile->buflen = 254;
      }
    }

  } else if (strcmp("wb", mode) == 0) {

    if ((rde = alloc_file_entry(di, rawname, type)) == NULL) {
      //puts("alloc_file_entry failed");
      return(NULL);
    }
    if ((imgfile = malloc(sizeof(*imgfile))) == NULL) {
      rde->type = 0;   /* don't leave a half-made entry in the directory */
      return(NULL);
    }
    imgfile->mode = 'w';
    imgfile->ts.track = 0;
    imgfile->ts.sector = 0;
    if ((imgfile->buffer = malloc(254)) == NULL) {
      free(imgfile);
      rde->type = 0;
      return(NULL);
    }
    imgfile->buflen = 254;
    di->modified = 1;

  } else {
    return(NULL);
  }

  imgfile->diskimage = di;
  imgfile->rawdirentry = rde;
  imgfile->position = 0;
  imgfile->bufptr = 0;

  ++(di->openfiles);
  set_status(di, 0, 0, 0);
  return(imgfile);
}


int di_read(ImageFile *imgfile, unsigned char *buffer, int len) {
  unsigned char *p;
  int bytesleft;
  int counter = 0;

  while (len) {
    /* a cyclic/over-long block chain cannot legitimately yield more bytes
       than the whole image; stop so callers that loop on di_read() == 254
       cannot be spun forever by a crafted image. */
    if (imgfile->position >= imgfile->diskimage->size) {
      return(counter);
    }
    bytesleft = imgfile->buflen - imgfile->bufptr;
    if (bytesleft == 0) {
      if (imgfile->nextts.track == 0) {
	return(counter);
      }
      imgfile->ts = next_ts_in_chain(imgfile->diskimage, imgfile->ts);
      if (imgfile->ts.track == 0) {
	/* the link pointed outside the image: end the file here with an
	 * error status instead of reading block 1/0 (another file) */
	set_status(imgfile->diskimage, 66, 0, 0);
	imgfile->nextts.track = 0;
	return(counter);
      }
      p = get_ts_addr(imgfile->diskimage, imgfile->ts);
      imgfile->buffer = p + 2;
      imgfile->nextts.track = p[0];
      imgfile->nextts.sector = p[1];
      if (imgfile->nextts.track == 0) {
	imgfile->buflen = (imgfile->nextts.sector >= 1) ? imgfile->nextts.sector - 1 : 0;
      } else {
	imgfile->buflen = 254;
      }
      imgfile->bufptr = 0;
    } else {
      if (len >= bytesleft) {
	while (bytesleft) {
	  *buffer++ = imgfile->buffer[imgfile->bufptr++];
	  --len;
	  --bytesleft;
	  ++counter;
	  ++(imgfile->position);
	}
      } else {
	while (len) {
	  *buffer++ = imgfile->buffer[imgfile->bufptr++];
	  --len;
	  --bytesleft;
	  ++counter;
	  ++(imgfile->position);
	}
      }
    }
  }
  return(counter);
}


int di_write(ImageFile *imgfile, unsigned char *buffer, int len) {
  unsigned char *p;
  int bytesleft;
  int counter = 0;

  while (len) {
    bytesleft = imgfile->buflen - imgfile->bufptr;
    if (bytesleft == 0) {
      if (imgfile->diskimage->blocksfree == 0) {
	set_status(imgfile->diskimage, 72, 0, 0);
	return(counter);
      }
      imgfile->nextts = alloc_next_ts(imgfile->diskimage, imgfile->ts);
      if (imgfile->nextts.track == 0) {
	/* no free block found (e.g. a crafted BAM whose free-count lies) —
	 * treat as disk full instead of writing into block 0 */
	set_status(imgfile->diskimage, 72, 0, 0);
	return(counter);
      }
      if (imgfile->ts.track == 0) {
	imgfile->rawdirentry->startts = imgfile->nextts;
      } else {
	p = get_ts_addr(imgfile->diskimage, imgfile->ts);
	p[0] = imgfile->nextts.track;
	p[1] = imgfile->nextts.sector;
      }
      imgfile->ts = imgfile->nextts;
      p = get_ts_addr(imgfile->diskimage, imgfile->ts);
      p[0] = 0;
      p[1] = 0xff;
      memcpy(p + 2, imgfile->buffer, 254);
      imgfile->bufptr = 0;
      if (++(imgfile->rawdirentry->sizelo) == 0) {
	++(imgfile->rawdirentry->sizehi);
      }
      --(imgfile->diskimage->blocksfree);
    } else {
      if (len >= bytesleft) {
	while (bytesleft) {
	  imgfile->buffer[imgfile->bufptr++] = *buffer++;
	  --len;
	  --bytesleft;
	  ++counter;
	  ++(imgfile->position);
	}
      } else {
	while (len) {
	  imgfile->buffer[imgfile->bufptr++] = *buffer++;
	  --len;
	  --bytesleft;
	  ++counter;
	  ++(imgfile->position);
	}
      }
    }
  }
  return(counter);
}


void di_close(ImageFile *imgfile) {
  unsigned char *p;

  if (imgfile->mode == 'w') {
    if (imgfile->bufptr == 0 && imgfile->ts.track == 0) {
      /* Nothing was ever written: CBM DOS still gives an empty file one
       * block with link 00 01, otherwise the entry has startts 0/0 and a
       * later read would alias block 1/0. Writing a zero-length buffer
       * through the normal path below does exactly that. */
      if (imgfile->diskimage->blocksfree) {
	imgfile->bufptr = 0;
	memset(imgfile->buffer, 0, 254);
	/* fall into the "bufptr" branch by forcing one (empty) last block */
	imgfile->nextts = alloc_next_ts(imgfile->diskimage, imgfile->ts);
	if (imgfile->nextts.track != 0) {
	  imgfile->rawdirentry->startts = imgfile->nextts;
	  imgfile->ts = imgfile->nextts;
	  p = get_ts_addr(imgfile->diskimage, imgfile->ts);
	  p[0] = 0;
	  p[1] = 1;
	  memset(p + 2, 0, 254);
	  if (++(imgfile->rawdirentry->sizelo) == 0) {
	    ++(imgfile->rawdirentry->sizehi);
	  }
	  --(imgfile->diskimage->blocksfree);
	  imgfile->rawdirentry->type |= 0x80;
	}
      }
    } else if (imgfile->bufptr) {
      if (imgfile->diskimage->blocksfree) {
	imgfile->nextts = alloc_next_ts(imgfile->diskimage, imgfile->ts);
	if (imgfile->nextts.track == 0) {
	  /* no free block (e.g. crafted BAM) — disk full, don't write block 0 */
	  set_status(imgfile->diskimage, 72, 0, 0);
	} else {
	  if (imgfile->ts.track == 0) {
	    imgfile->rawdirentry->startts = imgfile->nextts;
	  } else {
	    p = get_ts_addr(imgfile->diskimage, imgfile->ts);
	    p[0] = imgfile->nextts.track;
	    p[1] = imgfile->nextts.sector;
	  }
	  imgfile->ts = imgfile->nextts;
	  p = get_ts_addr(imgfile->diskimage, imgfile->ts);
	  p[0] = 0;
	  /* Last block: the link "sector" byte encodes the number of used data
	   * bytes as (used + 1) — this is how di_read reconstructs the final
	   * block length (buflen = sector - 1). It was hardcoded to 0xff, which
	   * reported 254 bytes for every last block, so any file whose size is
	   * not a multiple of 254 read back too long with the unwritten tail of
	   * the scratch buffer (uninitialized heap) appended. */
	  p[1] = (unsigned char)(imgfile->bufptr + 1);
	  memset(imgfile->buffer + imgfile->bufptr, 0, 254 - imgfile->bufptr);
	  memcpy(p + 2, imgfile->buffer, 254);
	  imgfile->bufptr = 0;
	  if (++(imgfile->rawdirentry->sizelo) == 0) {
	    ++(imgfile->rawdirentry->sizehi);
	  }
	  --(imgfile->diskimage->blocksfree);
	  imgfile->rawdirentry->type |= 0x80;
	}
      }
    } else {
      imgfile->rawdirentry->type |= 0x80;
    }
    if (!(imgfile->rawdirentry->type & 0x80)) {
      /* The write ran out of blocks (status 72) and the file is incomplete.
       * Don't leave a splat entry that owns every free block and can
       * neither be deleted nor overwritten: give the blocks back and drop
       * the entry, exactly like VALIDATE would. The caller keeps its source
       * data and can retry on another disk. */
      if (imgfile->rawdirentry->startts.track) {
	free_chain(imgfile->diskimage, imgfile->rawdirentry->startts);
      }
      memset((unsigned char *)imgfile->rawdirentry + 2, 0, 30);
      imgfile->rawdirentry->type = 0;
      imgfile->diskimage->blocksfree = blocks_free(imgfile->diskimage);
      imgfile->diskimage->modified = 1;
    }
    free(imgfile->buffer);
  }
  --(imgfile->diskimage->openfiles);
  free(imgfile);
}


int di_format(DiskImage *di, const unsigned char *rawname, const unsigned char *rawid) {
  unsigned char *p;
  TrackSector ts;

  di->modified = 1;

  switch (di->type) {

  case D64:
    /* erase disk */
    if (rawid) {
      memset(di->image, 0, 174848);
    }

    /* get ptr to bam */
    p = get_ts_addr(di, di->bam);

    /* setup header */
    p[0] = 18;
    p[1] = 1;
    p[2] = 'A';
    p[3] = 0;

    /* clear bam */
    memset(p + 4, 0, 0x8c);

    /* free blocks */
    for (ts.track = 1; ts.track <= di_tracks(di->type); ++ts.track) {
      for (ts.sector = 0; ts.sector < di_sectors_per_track(di->type, ts.track); ++ts.sector) {
	di_free_ts(di, ts);
      }
    }

    /* allocate bam and dir */
    ts.track = 18;
    ts.sector = 0;
    di_alloc_ts(di, ts);
    ts.sector = 1;
    di_alloc_ts(di, ts);

    /* copy name */
    memcpy(p + 0x90, rawname, 16);

    /* set id */
    memset(p + 0xa0, 0xa0, 2);
    if (rawid) {
      memcpy(p + 0xa2, rawid, 2);
    }
    memset(p + 0xa4, 0xa0, 7);
    p[0xa5] = '2';
    p[0xa6] = 'A';

    /* clear unused bytes */
    memset(p + 0xab, 0, 0x55);

    /* clear first dir block */
    memset(p + 256, 0, 256);
    p[257] = 0xff;
    break;

  case D71:
    /* erase disk */
    if (rawid) {
      memset(di->image, 0, 349696);
    }

    /* get ptr to bam2 */
    p = get_ts_addr(di, di->bam2);

    /* clear bam2 */
    memset(p, 0, 256);

    /* get ptr to bam */
    p = get_ts_addr(di, di->bam);

    /* setup header */
    p[0] = 18;
    p[1] = 1;
    p[2] = 'A';
    p[3] = 0x80;

    /* clear bam */
    memset(p + 4, 0, 0x8c);

    /* clear bam2 counters */
    memset(p + 0xdd, 0, 0x23);

    /* free blocks */
    for (ts.track = 1; ts.track <= di_tracks(di->type); ++ts.track) {
      if (ts.track != 53) {
	for (ts.sector = 0; ts.sector < di_sectors_per_track(di->type, ts.track); ++ts.sector) {
	  di_free_ts(di, ts);
	}
      }
    }

    /* allocate bam and dir */
    ts.track = 18;
    ts.sector = 0;
    di_alloc_ts(di, ts);
    ts.sector = 1;
    di_alloc_ts(di, ts);

    /* copy name */
    memcpy(p + 0x90, rawname, 16);

    /* set id */
    memset(p + 0xa0, 0xa0, 2);
    if (rawid) {
      memcpy(p + 0xa2, rawid, 2);
    }
    memset(p + 0xa4, 0xa0, 7);
    p[0xa5] = '2';
    p[0xa6] = 'A';

    /* clear unused bytes */
    memset(p + 0xab, 0, 0x32);

    /* clear first dir block */
    memset(p + 256, 0, 256);
    p[257] = 0xff;
    break;

  case D81:
    /* erase disk */
    if (rawid) {
      memset(di->image, 0, 819200);
    }

    /* get ptr to bam */
    p = get_ts_addr(di, di->bam);

    /* setup header */
    p[0] = 0x28;
    p[1] = 0x02;
    p[2] = 0x44;
    p[3] = 0xbb;
    p[6] = 0xc0;

    /* set id */
    if (rawid) {
      memcpy(p + 4, rawid, 2);
    }

    /* clear bam */
    memset(p + 7, 0, 0xf9);

    /* get ptr to bam2 */
    p = get_ts_addr(di, di->bam2);

    /* setup header */
    p[0] = 0x00;
    p[1] = 0xff;
    p[2] = 0x44;
    p[3] = 0xbb;
    p[6] = 0xc0;

    /* set id */
    if (rawid) {
      memcpy(p + 4, rawid, 2);
    }

    /* clear bam2 */
    memset(p + 7, 0, 0xf9);

    /* free blocks */
    for (ts.track = 1; ts.track <= di_tracks(di->type); ++ts.track) {
      for (ts.sector = 0; ts.sector < di_sectors_per_track(di->type, ts.track); ++ts.sector) {
	di_free_ts(di, ts);
      }
    }

    /* allocate bam and dir */
    ts.track = 40;
    ts.sector = 0;
    di_alloc_ts(di, ts);
    ts.sector = 1;
    di_alloc_ts(di, ts);
    ts.sector = 2;
    di_alloc_ts(di, ts);
    ts.sector = 3;
    di_alloc_ts(di, ts);

    /* get ptr to dir */
    p = get_ts_addr(di, di->dir);

    /* header: link to the first directory block (40/3), DOS version 'D'.
     * Without this link every "New disk image" D81 had an unreachable,
     * empty directory. */
    p[0] = 40;
    p[1] = 3;
    p[2] = 'D';
    p[3] = 0;

    /* copy name */
    memcpy(p + 4, rawname, 16);

    /* set id */
    memset(p + 0x14, 0xa0, 2);
    if (rawid) {
      memcpy(p + 0x16, rawid, 2);
    }
    memset(p + 0x18, 0xa0, 5);
    p[0x19] = '3';
    p[0x1a] = 'D';

    /* clear unused bytes */
    memset(p + 0x1d, 0, 0xe3);

    /* clear first dir block */
    memset(p + 768, 0, 256);
    p[769] = 0xff;
    break;

  }

  di->blocksfree = blocks_free(di);

  return(set_status(di, 0, 0, 0));
}


int di_delete(DiskImage *di, const unsigned char *rawpattern, FileType type) {
  RawDirEntry *rde;
  int delcount = 0;

  switch (type) {

  case T_SEQ:
  case T_PRG:
  case T_USR:
  case T_REL:
    while ((rde = find_file_entry(di, rawpattern, type))) {
      free_chain(di, rde->startts);
      if (type == T_REL && rde->relsidets.track) {
	free_chain(di, rde->relsidets);   /* side-sector chain */
      }
      rde->type = 0;
      di->modified = 1;
      ++delcount;
    }
    if (delcount) {
      di->blocksfree = blocks_free(di);   /* keep the cached count in step with the BAM */
      return(set_status(di, 1, delcount, 0));
    } else {
      return(set_status(di, 62, 0, 0));
    }
    break;

  default:
    return(set_status(di, 64, 0, 0));
    break;

  }
}


int di_rename(DiskImage *di, const unsigned char *oldrawname, const unsigned char *newrawname, FileType type) {
  RawDirEntry *rde;

  if (find_file_entry_exact(di, newrawname)) {
    return(set_status(di, 63, 0, 0));   /* file exists */
  }
  if ((rde = find_file_entry(di, oldrawname, type))) {
    memcpy(rde->rawname, newrawname, 16);
    di->modified = 1;
    return(set_status(di, 0, 0, 0));
  } else {
    return(set_status(di, 62, 0, 0));
  }
}


/* Delete the one entry whose raw name matches exactly (no wildcards, any
 * file type, splat entries included so a damaged image can be cleaned). */
int di_delete_exact(DiskImage *di, const unsigned char *rawname) {
  RawDirEntry *rde;

  if ((rde = find_file_entry_exact(di, rawname)) == NULL) {
    return(set_status(di, 62, 0, 0));
  }
  if (rde->startts.track) {
    free_chain(di, rde->startts);
  }
  if ((rde->type & 7) == T_REL && rde->relsidets.track) {
    free_chain(di, rde->relsidets);
  }
  rde->type = 0;
  di->modified = 1;
  di->blocksfree = blocks_free(di);
  return(set_status(di, 1, 1, 0));
}


int di_rename_exact(DiskImage *di, const unsigned char *oldrawname, const unsigned char *newrawname) {
  RawDirEntry *rde;

  if (find_file_entry_exact(di, newrawname)) {
    return(set_status(di, 63, 0, 0));
  }
  if ((rde = find_file_entry_exact(di, oldrawname)) == NULL) {
    return(set_status(di, 62, 0, 0));
  }
  memcpy(rde->rawname, newrawname, 16);
  di->modified = 1;
  return(set_status(di, 0, 0, 0));
}


/* Open an entry by exact raw name for reading, whatever its type. */
ImageFile *di_open_exact(DiskImage *di, const unsigned char *rawname) {
  RawDirEntry *rde;

  if ((rde = find_file_entry_exact(di, rawname)) == NULL) {
    set_status(di, 62, 0, 0);
    return(NULL);
  }
  /* di_open("rb") matches on (type | 0x80); reuse it with the entry's own
   * type and exact name - the wildcard matcher is exact for names without
   * '*' / '?' and those are handled by the direct lookup below. */
  if (memchr(rawname, '*', 16) == NULL && memchr(rawname, '?', 16) == NULL) {
    return(di_open(di, rawname, (FileType)(rde->type & 7), "rb"));
  }
  {
    /* name contains wildcard characters: build the ImageFile by hand */
    ImageFile *imgfile;
    unsigned char *p;

    if (!di_ts_valid(di, rde->startts)) {
      set_status(di, 66, rde->startts.track, rde->startts.sector);
      return(NULL);
    }
    if ((imgfile = malloc(sizeof(*imgfile))) == NULL) {
      return(NULL);
    }
    imgfile->mode = 'r';
    imgfile->ts = rde->startts;
    p = get_ts_addr(di, rde->startts);
    imgfile->buffer = p + 2;
    imgfile->nextts.track = p[0];
    imgfile->nextts.sector = p[1];
    if (imgfile->nextts.track == 0) {
      imgfile->buflen = (imgfile->nextts.sector >= 1) ? imgfile->nextts.sector - 1 : 0;
    } else {
      imgfile->buflen = 254;
    }
    imgfile->diskimage = di;
    imgfile->rawdirentry = rde;
    imgfile->position = 0;
    imgfile->bufptr = 0;
    ++(di->openfiles);
    set_status(di, 0, 0, 0);
    return(imgfile);
  }
}

/* allocate rawname and convert */
unsigned char *di_name_to_rawname(char *name) {
    unsigned char *rawname;
    int i;
    
    if ((rawname = malloc(16)) == NULL) {
        return(NULL);
    }
    memset(rawname, 0xa0, 16);
    for (i = 0; i < 16 && name[i]; ++i) {
        rawname[i] = name[i];
    }
    return(rawname);
}


/* ---- disk tools --------------------------------------------------------- */

/* Header byte layout (CBM DOS):
 *   D64/D71 header = BAM block 18/0:  0x90 name[16]  0xA2 id[2]  0xA5 dos[2]
 *   D81 header block 40/0:            0x04 name[16]  0x16 id[2]  0x19 dos[2]
 * The two BAM sectors of a D81 (40/1, 40/2) repeat the ID at offset 4. */
unsigned char *di_id(DiskImage *di) {
  unsigned char *p = get_ts_addr(di, di->dir);
  return(di->type == D81 ? p + 0x16 : p + 0xa2);
}


unsigned char *di_dostype(DiskImage *di) {
  unsigned char *p = get_ts_addr(di, di->dir);
  return(di->type == D81 ? p + 0x19 : p + 0xa5);
}


int di_rename_disk(DiskImage *di, const unsigned char *rawname16, const unsigned char *rawid2) {
  unsigned char *p = get_ts_addr(di, di->dir);

  if (rawname16) {
    memcpy(p + (di->type == D81 ? 0x04 : 0x90), rawname16, 16);
  }
  if (rawid2) {
    memcpy(di_id(di), rawid2, 2);
    if (di->type == D81) {
      /* 1581 DOS compares the ID stored in each BAM sector with the header
       * and reports a DOS mismatch when they differ, so change all three */
      memcpy(get_ts_addr(di, di->bam) + 4, rawid2, 2);
      memcpy(get_ts_addr(di, di->bam2) + 4, rawid2, 2);
    }
  }
  di->modified = 1;
  return(set_status(di, 0, 0, 0));
}


int di_set_locked(DiskImage *di, const unsigned char *rawname, int locked) {
  RawDirEntry *rde;

  if ((rde = find_file_entry_exact(di, rawname)) == NULL) {
    return(set_status(di, 62, 0, 0));
  }
  if (locked) {
    rde->type |= 0x40;
  } else {
    rde->type &= ~0x40;
  }
  di->modified = 1;
  return(set_status(di, 0, 0, 0));
}


int di_entry_info(DiskImage *di, const unsigned char *rawname, int *type, int *blocks, int *closed, int *locked) {
  RawDirEntry *rde;

  if ((rde = find_file_entry_exact(di, rawname)) == NULL) {
    return(set_status(di, 62, 0, 0));
  }
  if (type)   *type = rde->type & 7;
  if (blocks) *blocks = rde->sizehi << 8 | rde->sizelo;
  if (closed) *closed = (rde->type & 0x80) ? 1 : 0;
  if (locked) *locked = (rde->type & 0x40) ? 1 : 0;
  return(set_status(di, 0, 0, 0));
}


/* Allocate every block of a chain in the (freshly cleared) BAM. The chain
 * ends at the first link that is invalid, or at a block that is already
 * allocated: that is a cross-link into another file or a loop back into
 * this one, and CBM DOS would stop there too (71, DIR ERROR). Returns the
 * number of blocks allocated. */
static int validate_alloc_chain(DiskImage *di, TrackSector ts) {
  int n = 0;

  while (ts.track && di_ts_valid(di, ts) && n < di_blocks(di->type)) {
    if (!di_is_ts_free(di, ts)) {
      break;
    }
    di_alloc_ts(di, ts);
    ++n;
    ts = next_ts_in_chain(di, ts);
  }
  return(n);
}


int di_validate(DiskImage *di, int *fixed_blocks, int *removed_entries) {
  unsigned char *p;
  TrackSector ts;
  RawDirEntry *rde;
  int before, ndir, hops, offset, removed = 0;

  before = blocks_free(di);

  /* 1. Clear the allocation maps so every block reads as "allocated" with
   *    a zero free count, then free every data block by geometry. Going
   *    through di_free_ts() keeps the per-track counts exact whatever
   *    garbage the old BAM held (a zeroed, or an all-0xFF, BAM). */
  switch (di->type) {
  case D64:
    p = get_ts_addr(di, di->bam);
    memset(p + 4, 0, 35 * 4);          /* tracks 1-35: count + 3 bitmap bytes */
    break;
  case D71:
    p = get_ts_addr(di, di->bam);
    memset(p + 4, 0, 35 * 4);          /* side 0 */
    memset(p + 0xdd, 0, 35);           /* side 1 free counts (tracks 36-70) */
    p = get_ts_addr(di, di->bam2);
    memset(p, 0, 35 * 3);              /* side 1 bitmaps in 53/0 */
    break;
  case D81:
    p = get_ts_addr(di, di->bam);
    memset(p + 16, 0, 40 * 6);         /* tracks 1-40: count + 5 bitmap bytes */
    p = get_ts_addr(di, di->bam2);
    memset(p + 16, 0, 40 * 6);         /* tracks 41-80 */
    break;
  }
  for (ts.track = 1; ts.track <= di_tracks(di->type); ++ts.track) {
    if (di->type == D71 && ts.track == 53) {
      continue;   /* 1571 keeps the whole second-side BAM track reserved */
    }
    for (ts.sector = 0; ts.sector < di_sectors_per_track(di->type, ts.track); ++ts.sector) {
      di_free_ts(di, ts);
    }
  }

  /* 2. Re-allocate the system blocks: the BAM sectors of a D81 live apart
   *    from the directory header (on D64/D71 header and BAM are the same
   *    block, allocated with the directory chain below). */
  if (di->type == D81) {
    di_alloc_ts(di, di->bam);
    di_alloc_ts(di, di->bam2);
  }

  /* 3. Header block plus the directory chain hanging off it. ndir bounds
   *    the entry walk below to exactly the blocks that were reachable. */
  ndir = validate_alloc_chain(di, di->dir);

  /* 4. Every closed entry keeps its data chain (and REL side sectors);
   *    unclosed entries were left by an interrupted write and are dropped,
   *    their blocks simply stay free. */
  ts = next_ts_in_chain(di, di->dir);
  for (hops = 1; ts.track && hops < ndir; hops++) {
    p = get_ts_addr(di, ts);
    for (offset = 0; offset < 256; offset += 32) {
      rde = (RawDirEntry *)(p + offset);
      if (rde->type == 0) {
	continue;
      }
      if (!(rde->type & 0x80)) {
	memset((unsigned char *)rde + 2, 0, 30);   /* keep the block link in bytes 0-1 */
	rde->type = 0;
	++removed;
	continue;
      }
      validate_alloc_chain(di, rde->startts);
      if ((rde->type & 7) == T_REL && rde->relsidets.track) {
	validate_alloc_chain(di, rde->relsidets);
      }
    }
    ts = next_ts_in_chain(di, ts);
  }

  di->blocksfree = blocks_free(di);
  di->modified = 1;
  if (fixed_blocks) {
    *fixed_blocks = di->blocksfree - before;
  }
  if (removed_entries) {
    *removed_entries = removed;
  }
  return(set_status(di, 0, 0, 0));
}
