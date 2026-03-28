#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <SDL.h>
#ifdef WINDOWS
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "net.h"
#include "timer.h"
#include "gfx.h"
#include "menu.h"
#include "xfer.h"
#include "xmodem.h"
#include "punter.h"
#include "rainbow.h"
#include "diskimage.h"
#include "config.h"

#define XFER_PATH_MAX 1024

char xfer_tempdlname[XFER_PATH_MAX] = "";
char xfer_tempulname[XFER_PATH_MAX] = "upload.tmp";
char xfer_filename[256];

FILE *xfer_sendfile, *xfer_recvfile;

Direction xfer_direction;
Protocol xfer_protocol;
int xfer_cancel;
int xfer_saved_bytes;
int xfer_file_size;
unsigned char xfer_buffer[4096];
unsigned int xfer_starttime;
unsigned int xfer_last_kbd_check;

static int xfer_progress_total_for_display(int current, int total) {
  int shown_total = total;

  if (shown_total > 0) {
    if (current > shown_total) {
      current = shown_total;
    }
    return shown_total;
  }

  if (current < 0) {
    current = 0;
  }

  shown_total = current + 1024;
  if (shown_total < 1024) {
    shown_total = 1024;
  }
  return shown_total;
}

void xfer_progress_status(const char *message, int current, int total) {
  int shown_current = current;
  int shown_total = xfer_progress_total_for_display(current, total);

  if (shown_current < 0) {
    shown_current = 0;
  }
  if (total > 0 && shown_current > total) {
    shown_current = total;
  }
  if (shown_current > shown_total) {
    shown_current = shown_total;
  }

  menu_update_xfer_progress((char *)message, shown_current, shown_total);
  gfx_vbl();
}

void xfer_progress(const char *message) {
  xfer_progress_status(message, xfer_saved_bytes, xfer_file_size);
}


void xfer_save_file(char *filename);

static void xfer_log_errno(const char *prefix, const char *path) {
  if (path && *path) {
    printf("%s: %s (%s)\n", prefix, path, strerror(errno));
  } else {
    printf("%s: %s\n", prefix, strerror(errno));
  }
}

static void xfer_cleanup_temp_download(void) {
  if (xfer_recvfile != NULL) {
    fclose(xfer_recvfile);
    xfer_recvfile = NULL;
  }
  if (xfer_tempdlname[0]) {
    remove(xfer_tempdlname);
    xfer_tempdlname[0] = 0;
  }
}

static int xfer_create_temp_download(void) {
#ifdef WINDOWS
  char tempPath[MAX_PATH];
  char tempFile[MAX_PATH];

  if (GetTempPathA(MAX_PATH, tempPath) == 0) {
    return 0;
  }
  if (GetTempFileNameA(tempPath, "cgt", 0, tempFile) == 0) {
    return 0;
  }

  strncpy(xfer_tempdlname, tempFile, sizeof(xfer_tempdlname) - 1);
  xfer_tempdlname[sizeof(xfer_tempdlname) - 1] = 0;
  return 1;
#else
  int fd;

  snprintf(xfer_tempdlname, sizeof(xfer_tempdlname), "/tmp/cgterm-download-XXXXXX");
  fd = mkstemp(xfer_tempdlname);
  if (fd < 0) {
    xfer_tempdlname[0] = 0;
    return 0;
  }
  close(fd);
  return 1;
#endif
}

static int xfer_open_temp_download(void) {
  if (!xfer_create_temp_download()) {
    menu_draw_message("Couldn't create tempfile!");
    menu_show();
    gfx_vbl();
    xfer_log_errno("Could not create tempfile", NULL);
    return 0;
  }

  xfer_recvfile = fopen(xfer_tempdlname, "wb+");
  if (xfer_recvfile == NULL) {
    menu_draw_message("Couldn't open tempfile!");
    menu_show();
    gfx_vbl();
    xfer_log_errno("Could not open tempfile", xfer_tempdlname);
    xfer_cleanup_temp_download();
    return 0;
  }

  return 1;
}

static int xfer_copy_file(FILE *from, FILE *to, int bytesleft) {
  unsigned char filebuf[4096];
  int l;

  while (bytesleft > 0) {
    l = bytesleft > (int)sizeof(filebuf) ? (int)sizeof(filebuf) : bytesleft;
    l = (int)fread(filebuf, 1, l, from);
    if (l <= 0) {
      return 0;
    }
    if ((int)fwrite(filebuf, 1, l, to) != l) {
      return 0;
    }
    bytesleft -= l;
  }
  return (bytesleft == 0);
}

static const char *multipunter_ext_from_type(int filetype) {
  switch (filetype) {
  case 1:
    return ".seq";
  case 2:
    return ".prg";
  case 3:
    return ".wp";
  default:
    return "";
  }
}

static void multipunter_sanitize_filename(const char *src, char *dst, size_t dstsz, int filetype, int fileno) {
  size_t di = 0;
  int sawdot = 0;

  if (dstsz == 0) {
    return;
  }

  while (*src == ' ') {
    ++src;
  }

  while (*src && di + 1 < dstsz) {
    unsigned char c = (unsigned char) *src++;

    if (c < 32) {
      continue;
    }
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      c = '_';
    }
    if (c == ',') {
      c = '.';
    }
    if (c == '.') {
      sawdot = 1;
    }
    dst[di++] = (char) c;
  }

  while (di && (dst[di - 1] == ' ' || dst[di - 1] == '.')) {
    --di;
  }
  dst[di] = 0;

  if (di == 0) {
    snprintf(dst, dstsz, "mpunter_%03d%s", fileno, multipunter_ext_from_type(filetype));
    return;
  }

  if (!sawdot && multipunter_ext_from_type(filetype)[0] && strlen(dst) + strlen(multipunter_ext_from_type(filetype)) + 1 < dstsz) {
    strcat(dst, multipunter_ext_from_type(filetype));
  }
}

static int multipunter_read_announcement(char *namebuf, size_t namebufsz, int *is_batch_end) {
  signed int c;
  size_t pos = 0;

  *is_batch_end = 0;
  if (namebufsz) {
    namebuf[0] = 0;
  }

  xfer_progress("Multi Punter: waiting for filename...");

  while (!xfer_cancel) {
    c = xfer_recv_byte(1000);
    if (c < 0) {
      continue;
    }
    if (c == 0x09) {
      break;
    }
  }

  if (xfer_cancel) {
    return 0;
  }

  while (!xfer_cancel) {
    c = xfer_recv_byte(1000);
    if (c < 0) {
      continue;
    }
    if (c == 0x04 && pos == 0) {
      *is_batch_end = 1;
      return 1;
    }

    if (c == '\r' || c == '\n') {
      namebuf[pos] = 0;

      while ((c = xfer_recv_byte(10)) >= 0) {
        if (c != '\r' && c != '\n') {
          break;
        }
      }

      return 1;
    }

    if (pos + 1 < namebufsz) {
      namebuf[pos++] = (char) c;
      namebuf[pos] = 0;
    }
  }

  return 0;
}

static int multipunter_recv(void) {
  int filecount = 0;
  char remote_name[256];
  char local_name[256];
  int batch_end = 0;
  char msg[256];

  xfer_filename[0] = 0;

  while (!xfer_cancel) {
    if (!multipunter_read_announcement(remote_name, sizeof(remote_name), &batch_end)) {
      xfer_cleanup_temp_download();
      menu_draw_message("Multi Punter failed");
      menu_show();
      gfx_vbl();
      return 0;
    }

    if (batch_end) {
      if (filecount == 0) {
        menu_draw_message("Multi Punter: empty batch");
        menu_show();
        gfx_vbl();
        return 0;
      }
      snprintf(msg, sizeof(msg), "Received %d Multi Punter file%s", filecount, filecount == 1 ? "" : "s");
      menu_draw_message(msg);
      menu_show();
      gfx_vbl();
      return 1;
    }

    if (!xfer_open_temp_download()) {
      return 0;
    }

    snprintf(msg, sizeof(msg), "Multi Punter: %s", remote_name);
    xfer_progress_status(msg, 0, 0);

    xfer_saved_bytes = 0;
    xfer_starttime = timer_get_ticks();

    if (!punter_recv()) {
      xfer_cleanup_temp_download();
      menu_draw_message("Multi Punter receive failed");
      menu_show();
      gfx_vbl();
      return 0;
    }

    fclose(xfer_recvfile);
    xfer_recvfile = NULL;

    multipunter_sanitize_filename(remote_name, local_name, sizeof(local_name), punter_last_filetype, filecount + 1);
    snprintf(xfer_filename, 256, "%s", local_name);
    xfer_save_file(local_name);
    ++filecount;
  }

  xfer_cleanup_temp_download();
  menu_draw_message("Multi Punter cancelled");
  menu_show();
  gfx_vbl();
  return 0;
}

void xfer_check_kbd(void) {
  SDL_Event event;

  if (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      exit(1);
      break;
    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_ESCAPE) {
        xfer_cancel = 1;
      }
      break;
    }
  }
}

void xfer_send_byte(unsigned char c) {
  unsigned int t;

  t = timer_get_ticks();
  if (t > xfer_last_kbd_check + 20) {
    xfer_last_kbd_check = t;
    xfer_check_kbd();
  }
  net_send(c);
}

signed int xfer_recv_byte(int timeout) {
  signed int c;
  unsigned int starttime, t;

  starttime = timer_get_ticks();
  while ((c = net_receive()) == -1) {
    if (timer_get_ticks() > starttime + timeout) {
      t = timer_get_ticks() - xfer_starttime;
      printf("xfer_recv_byte[%3d.%03d]: timeout\n", t / 1000, t % 1000);
      return(-1);
    } else {
      timer_delay(1);
      if (timer_get_ticks() > xfer_last_kbd_check + 20) {
        xfer_check_kbd();
        xfer_last_kbd_check = timer_get_ticks();
      }
    }
  }

  return(c);
}

signed int xfer_recv_byte_error(int timeout, int errorcnt) {
  signed int c;

  while ((c = xfer_recv_byte(timeout)) == -1 && errorcnt) {
    --errorcnt;
  }
  return(c);
}

int xfer_save_data(unsigned char *data, int length) {
  int l;
  int written = 0;

  while (written < length) {
    l = (int)fwrite(data + written, 1, length - written, xfer_recvfile);
    if (l > 0) {
      written += l;
      continue;
    }

    xfer_log_errno("xfer_save_data failed", xfer_tempdlname);
    return(0);
  }

  if (fflush(xfer_recvfile) != 0) {
    xfer_log_errno("xfer_save_data flush failed", xfer_tempdlname);
    return(0);
  }

  xfer_saved_bytes += written;
  return(written);
}

int xfer_load_data(unsigned char *data, int length) {
  int l;
  int read = 0;

  while (read < length) {
    if ((l = (int)fread(data + read, 1, length - read, xfer_sendfile))) {
      read += l;
    } else {
      if (ferror(xfer_sendfile)) {
        xfer_log_errno("xfer_load_data failed", NULL);
        return(0);
      } else {
        return(read);
      }
    }
  }
  return(read);
}

int xfer_recv(void) {
  int status = 0;

  xfer_filename[0] = 0;
  xfer_cancel = 0;
  xfer_saved_bytes = 0;
  xfer_starttime = timer_get_ticks();
  menu_draw_xfer_progress("No filename", xfer_direction, xfer_protocol);
  menu_show();
  gfx_vbl();

  if (xfer_protocol == PROT_MULTIPUNTER) {
    return multipunter_recv();
  }

  if (!xfer_open_temp_download()) {
    return 0;
  }

  if (xfer_protocol == PROT_XMODEM) {
    status = xmodem_recv(0);
  } else if (xfer_protocol == PROT_XMODEMCRC) {
    status = xmodem_recv(1);
  } else if (xfer_protocol == PROT_XMODEM1K) {
    status = xmodem_recv(1);
  } else if (xfer_protocol == PROT_PUNTER) {
    status = punter_recv();
  } else if (xfer_protocol == PROT_RAINBOW) {
    status = rainbow_recv();
  }

  if (xfer_recvfile != NULL) {
    fclose(xfer_recvfile);
    xfer_recvfile = NULL;
  }

  if (!status) {
    xfer_cleanup_temp_download();
  }

  return(status);
}

int xfer_copy_from_image(char *imgname, char *src, char *dest) {
  DiskImage *di;
  ImageFile *imgfile;
  unsigned char rawname[16];
  FILE *outh;
  unsigned char buffer[4096];
  int l;

  if ((di = di_load_image(imgname)) == NULL) {
    return(0);
  }

  di_rawname_from_name(rawname, src);

  if ((imgfile = di_open(di, rawname, T_PRG, "rb")) == NULL) {
    di_free_image(di);
    return(0);
  }

  if ((outh = fopen(dest, "wb")) == NULL) {
    di_close(imgfile);
    di_free_image(di);
    return(-1);
  }

  while ((l = (int)di_read(imgfile, buffer, 4096))) {
    if ((int)fwrite(buffer, 1, l, outh) != l) {
      di_close(imgfile);
      di_free_image(di);
      fclose(outh);
      return(0);
    }
  }

  di_close(imgfile);
  di_free_image(di);
  fclose(outh);
  return(1);
}

void xfer_send(char *filename) {
  char name[256];
  char *p;
  int deletetmp = 0;

  xfer_cancel = 0;
  xfer_starttime = timer_get_ticks();
  menu_draw_xfer_progress(filename, xfer_direction, xfer_protocol);
  menu_show();
  gfx_vbl();

  if ((p = strrchr(cfg_xferdir, '.')) && strlen(p) == 4 && (p[1] == 'd' || p[1] == 'D') && isdigit(p[2]) && isdigit(p[3])) {
    if (xfer_copy_from_image(cfg_xferdir, filename, xfer_tempulname)) {
      snprintf(name, sizeof(name), "%s", xfer_tempulname);
      deletetmp = 1;
    } else {
      menu_draw_message("Couldn't open file!");
      menu_show();
      printf("Attempt to open file (%s) failed in xfer_send()\n", filename);
      gfx_vbl();
      return;
    }
  } else {
    snprintf(name, sizeof(name), "%s%c%s", cfg_xferdir,
#ifdef WINDOWS
      '\\',
#else
      '/',
#endif
      filename);
  }

  if ((xfer_sendfile = fopen(name, "rb"))) {
    if (fseek(xfer_sendfile, 0, SEEK_END)) {
      fclose(xfer_sendfile);
      menu_draw_message("Couldn't read file size!");
      menu_show();
      gfx_vbl();
      return;
    }
    xfer_file_size = (int)ftell(xfer_sendfile);
    fseek(xfer_sendfile, 0, SEEK_SET);

    if (xfer_protocol == PROT_XMODEM) {
      xmodem_send(0);
    } else if (xfer_protocol == PROT_XMODEMCRC) {
      xmodem_send(0);
    } else if (xfer_protocol == PROT_XMODEM1K) {
      xmodem_send(1);
    } else if (xfer_protocol == PROT_PUNTER) {
      punter_send();
    } else if (xfer_protocol == PROT_RAINBOW) {
      rainbow_send(filename);
    } else if (xfer_protocol == PROT_MULTIPUNTER) {
      menu_draw_message("Multi Punter send not implemented");
      gfx_vbl();
    }
    fclose(xfer_sendfile);
  } else {
    menu_draw_message("Couldn't open file!");
    menu_show();
    gfx_vbl();
  }

  if (deletetmp) {
    remove(name);
  }
}

void xfer_save_file_in_image(char *filename) {
  FILE *from;
  DiskImage *di;
  ImageFile *to;
  unsigned char filebuf[4096];
  char msgbuf[4096];
  int bytesleft, l;
  unsigned char rawname[16];

  if ((di = di_load_image(cfg_dldir)) == NULL) {
    menu_draw_message("Couldn't open disk image");
    menu_show();
    gfx_vbl();
    return;
  }

  if ((from = fopen(xfer_tempdlname, "rb")) == NULL) {
    di_free_image(di);
    menu_draw_message("Couldn't open temp file!");
    menu_show();
    xfer_log_errno("Could not open temp file", xfer_tempdlname);
    gfx_vbl();
    return;
  }

  di_rawname_from_name(rawname, filename);

  to = di_open(di, rawname, T_PRG, "wb");
  if (to == NULL) {
    fclose(from);
    di_free_image(di);
    menu_draw_message("Couldn't write file!");
    menu_show();
    gfx_vbl();
    return;
  }

  bytesleft = xfer_saved_bytes;
  while (bytesleft > 0) {
    l = bytesleft > (int)sizeof(filebuf) ? (int)sizeof(filebuf) : bytesleft;
    l = (int)fread(filebuf, 1, l, from);
    if (l <= 0) {
      menu_draw_message("Read error!");
      goto done;
    }
    if (di_write(to, filebuf, l) != l) {
      menu_draw_message("Write error!");
      goto done;
    }
    bytesleft -= l;
  }

  remove(xfer_tempdlname);
  xfer_tempdlname[0] = 0;

  snprintf(msgbuf, sizeof(msgbuf), "Saved %-24s", filename);
  menu_draw_message(msgbuf);

 done:
  menu_show();
  gfx_vbl();
  fclose(from);
  di_close(to);
  di_free_image(di);
}

static int xfer_ensure_dldir(void) {
  struct stat st;

  if (stat(cfg_dldir, &st) == 0) {
    return 1;  /* exists */
  }

  /* Directory doesn't exist — try to create it */
  printf("Download directory does not exist: %s\n", cfg_dldir);
  printf("Creating download directory...\n");

#ifdef WINDOWS
  if (_mkdir(cfg_dldir) == 0) {
#else
  if (mkdir(cfg_dldir, 0755) == 0) {
#endif
    printf("Created: %s\n", cfg_dldir);
    return 1;
  }

  printf("Could not create %s: %s\n", cfg_dldir, strerror(errno));
  /* Fall back to current directory */
  getcwd(cfg_dldir, 256);
  printf("Using fallback: %s\n", cfg_dldir);
  return 1;
}

void xfer_save_file_in_dir(char *filename) {
  FILE *from, *to;
  char msgbuf[4096];
  char name[256];

  if (!xfer_ensure_dldir()) {
    menu_draw_message("No download directory!");
    menu_show();
    gfx_vbl();
    return;
  }

  if ((from = fopen(xfer_tempdlname, "rb")) == NULL) {
    menu_draw_message("Couldn't open temp file!");
    menu_show();
    xfer_log_errno("Could not open temp file", xfer_tempdlname);
    gfx_vbl();
    return;
  }

  snprintf(name, sizeof(name), "%s%c%s", cfg_dldir,
#ifdef WINDOWS
    '\\',
#else
    '/',
#endif
    filename);

  to = fopen(name, "wb");
  if (to == NULL) {
    fclose(from);
    menu_draw_message("Couldn't write file!");
    menu_show();
    xfer_log_errno("Could not open destination file", name);
    gfx_vbl();
    return;
  }

  if (!xfer_copy_file(from, to, xfer_saved_bytes)) {
    menu_draw_message("Write error!");
    menu_show();
    gfx_vbl();
    fclose(from);
    fclose(to);
    return;
  }

  fclose(to);
  fclose(from);
  remove(xfer_tempdlname);
  xfer_tempdlname[0] = 0;

  snprintf(msgbuf, sizeof(msgbuf), "Saved %-24s", filename);
  menu_draw_message(msgbuf);
  menu_show();
  gfx_vbl();
}

void xfer_save_file(char *filename) {
  char *p;

  /* Check if download target is a disk image (.d64/.d71/.d81) */
  if ((p = strrchr(cfg_dldir, '.'))) {
    if (strlen(p) == 4) {
      if (p[1] == 'd' || p[1] == 'D') {
        if (isdigit(p[2]) && isdigit(p[3])) {
          xfer_save_file_in_image(filename);
          return;
        }
      }
    }
  }
  xfer_save_file_in_dir(filename);
}

