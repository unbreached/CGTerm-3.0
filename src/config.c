#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#ifdef WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "config.h"
#include "paths.h"
#include "gfx.h"


#ifdef WINDOWS
#define DIRCHAR '\\'
#else
#define DIRCHAR '/'
#endif

/* Helper function to validate hostname */
static int validate_hostname(const char *hostname) {
  size_t len = strlen(hostname);
  const char *p;

  if (len == 0 || len > 253) return 0;  /* RFC limits */

  for (p = hostname; *p; p++) {
    if (!isalnum(*p) && *p != '.' && *p != '-') {
      return 0;  /* Invalid character */
    }
  }

  /* Don't allow hostname to start/end with hyphen or be all dots */
  if (hostname[0] == '-' || hostname[len-1] == '-' || strspn(hostname, ".") == len) {
    return 0;
  }

  return 1;
}

/* Helper function to validate alias */
static int validate_alias(const char *alias) {
  size_t len = strlen(alias);
  const char *p;

  if (len == 0 || len > 64) return 0;  /* Reasonable limit */

  for (p = alias; *p; p++) {
    if (*p < 32 || *p == 127) {  /* No control characters */
      return 0;
    }
  }

  return 1;
}


int cfg_read = 0;

char *cfg_prefix = NULL;
char *cfg_homedir = NULL;

char *cfg_host = NULL;
unsigned int cfg_port = 23;
char *cfg_keyboard = NULL;
int cfg_zoom = 2;
char *cfg_logfile = NULL;
int cfg_fullscreen = 0;
int cfg_localecho = 0;
int cfg_senddelay = 0;
int cfg_recvdelay = 0;
int cfg_reconnect = 0;
unsigned int cfg_nextreconnect = 0;
int cfg_columns = 40;
int cfg_rows = 25;
int cfg_sound = 1;
int cfg_numbookmarks = 0;
char *cfg_bookmark_alias[40];
char *cfg_bookmark_host[40];
int cfg_bookmark_port[40];
char cfg_xferdir[256];
char cfg_dldir[256];
char cfg_seqdir[256];
char cfg_screendir[256];
int cfg_termmode = 0;
char cfg_connect_name[128] = "";
int cfg_charset = 0;
int cfg_bookmark_termmode[40];
int cfg_editmode = 0;
int cfg_debugmode = 0;
int cfg_splash = 1;
int cfg_modem = 0;
int cfg_splashfont = 6;  /* C64 Pro Mono */
int cfg_menufont = 8;  /* Edit Undo */
#ifdef WINDOWS
char cfg_bookmarkfile[256] = "cgterm-bookmarks.cfg";
#else
char cfg_bookmarkfile[256] = "~/.cgterm-bookmarks";
#endif

char host[256];
char keyboard[256];
char logfile[256];
char prefix[256];

FILE *fh = NULL;


static const char *cfg_default_keyboard_profile(void) {
  /* With unicode-first input, one keyboard file works for all
   * platforms and languages. Language is handled by charset ROM
   * selection and SDL unicode input. */
  return "default.kbd";
}

int cfg_init(char *argv0) {
#ifdef WINDOWS
  /* Try USERPROFILE first, then HOMEDRIVE+HOMEPATH, then current dir */
  if ((cfg_homedir = getenv("USERPROFILE")) == NULL) {
    const char *homedrive = getenv("HOMEDRIVE");
    const char *homepath = getenv("HOMEPATH");
    static char win_home[512];
    if (homedrive && homepath) {
      snprintf(win_home, sizeof(win_home), "%s%s", homedrive, homepath);
      cfg_homedir = win_home;
    } else {
      printf("No home directory found, using current directory\n");
      cfg_homedir = ".";
    }
  }
#else
  if ((cfg_homedir = getenv("HOME")) == NULL) {
    printf("$HOME is not set, using current directory\n");
    cfg_homedir = ".";
  }
#endif

  path_init(argv0);
  snprintf(prefix, sizeof(prefix), "%s", path_asset_root());
  cfg_prefix = prefix;
  path_build_asset(keyboard, sizeof(keyboard), cfg_default_keyboard_profile());
  cfg_keyboard = keyboard;

  /* Charset defaults to US/UK (0). Swedish/German ROMs replace PETSCII
   * graphics chars with ÅÖÄ/ÄÖÜ which breaks PETSCII art on most BBSes.
   * Users can switch charset via menu (H) or config: charset = swedish */

  /* Upload default: home directory */
#ifdef WINDOWS
  {
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
      snprintf(cfg_xferdir, sizeof(cfg_xferdir), "%s", userprofile);
    } else {
      getcwd(cfg_xferdir, sizeof(cfg_xferdir));
    }
  }
#else
  if (cfg_homedir && cfg_homedir[0] != '.') {
    snprintf(cfg_xferdir, sizeof(cfg_xferdir), "%s", cfg_homedir);
  } else {
    getcwd(cfg_xferdir, sizeof(cfg_xferdir));
  }
#endif

  /* Download default: ~/Downloads or %USERPROFILE%\Downloads */
#ifdef WINDOWS
  {
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
      snprintf(cfg_dldir, sizeof(cfg_dldir), "%s\\Downloads", userprofile);
    } else {
      getcwd(cfg_dldir, sizeof(cfg_dldir));
    }
  }
#else
  if (cfg_homedir && cfg_homedir[0] != '.') {
    snprintf(cfg_dldir, sizeof(cfg_dldir), "%s/Downloads", cfg_homedir);
  } else {
    getcwd(cfg_dldir, sizeof(cfg_dldir));
  }
#endif

  /* SEQ files and screenshots default to the download directory */
  snprintf(cfg_seqdir, sizeof(cfg_seqdir), "%s", cfg_dldir);
  snprintf(cfg_screendir, sizeof(cfg_screendir), "%s", cfg_dldir);

  return(0);
}


void real_cfg_change_dir(char *dirbuffer, const char *newdir) {
  int l, n;
  char *p;
  char newdirbuf[1024];

  snprintf(newdirbuf, sizeof(newdirbuf), "%s", newdir);
  newdir = newdirbuf;

  n = strlen(newdir);
  if (n == 0 || (n == 1 && strcmp(".", newdir) == 0)) {
    return;
  }
#ifdef WINDOWS
  /* Strip brackets from drive entries like [D:\] */
  if (n > 2 && newdirbuf[0] == '[' && newdirbuf[n-1] == ']') {
    memmove(newdirbuf, newdirbuf + 1, n - 2);
    newdirbuf[n - 2] = 0;
    n -= 2;
    newdir = newdirbuf;
  }
  if (n > 0 && newdir[n - 1] == DIRCHAR) {
    newdirbuf[n - 1] = 0;
    n--;
  }
  if (n > 1 && newdir[1] == ':' && isalpha((unsigned char)newdir[0])) {
    snprintf(dirbuffer, 256, "%s\\", newdir);
    return;
  }
#else
  // Replaces dir if first char is / or ~
  if (newdir[0] == '/' || newdir[0] == '~') {
    snprintf(dirbuffer, 256, "%s", newdir);
    return;
  }
  if (newdir[n - 1] == DIRCHAR) {
    newdirbuf[n - 1] = 0;
    n--;
  }
#endif
  l = strlen(dirbuffer);
  if (l == 0 || (l == 1 && strcmp(".", dirbuffer) == 0)) {
    snprintf(dirbuffer, 256, "%s", newdir);
    return;
  }
  if (n == 2 && strcmp("..", newdir) == 0) {
    if ((p = strrchr(dirbuffer, DIRCHAR))) {
      if (p != dirbuffer) {
#ifdef WINDOWS
	if (p == dirbuffer + 2 && dirbuffer[1] == ':') {
	  dirbuffer[3] = 0;
	  return;
	}
#endif
	*p = 0;
	return;
      } else {
	dirbuffer[1] = 0;
	return;
      }
    }
  }
  if (dirbuffer[l - 1] != DIRCHAR) {
    /* Need room for separator + null. dirbuffer is a 256-byte fixed array. */
    if (l + 1 >= 256) {
      return;
    }
    dirbuffer[l++] = DIRCHAR;
    dirbuffer[l] = 0;
  }
  {
    size_t used = strlen(dirbuffer);
    if (used + 1 < 256) {
      size_t remaining = 256 - used;
      strncat(dirbuffer, newdir, remaining - 1);
    }
  }
}

int cfg_change_dir(char *dirbuffer, const char *newdir) {
  real_cfg_change_dir(dirbuffer, newdir);
  return(1);
}



int cfg_file_exists(char *filename) {
  if ((fh = fopen(filename, "r")) == NULL) {
    return(0);
  } else {
    fclose(fh);
    return(1);
  }
}


void cfg_sethost(char *h) {
  snprintf(host, sizeof(host), "%s", h);
  cfg_host = host;
}


void addhost(int num, char *alias, char *hostname, int port) {
    char *ptr;
    char *chr;
    char _debugMsg[256];

  snprintf(_debugMsg, sizeof(_debugMsg), "adding %s (%s:%d)", alias, hostname, port);
    cfg_debug(_debugMsg);
  if ((ptr = malloc(strlen(hostname) + 1)) == NULL) {
    printf("addhost: malloc failed for hostname\n");
    cfg_bookmark_host[num] = NULL;
    cfg_bookmark_alias[num] = NULL;
    cfg_bookmark_port[num] = 0;
    return;
  }
  strcpy(ptr, hostname);
  if ((chr = strchr(ptr, ','))) {
    *chr = 0;
  }
  cfg_bookmark_host[num] = ptr;

  if ((ptr = malloc(strlen(alias) + 1)) == NULL) {
    printf("addhost: malloc failed for alias\n");
    free(cfg_bookmark_host[num]);
    cfg_bookmark_host[num] = NULL;
    cfg_bookmark_alias[num] = NULL;
    cfg_bookmark_port[num] = 0;
    return;
  }
  strcpy(ptr, alias);
  if (strlen(alias) > 27) {
    ptr[27] = 0;
  }
  if ((chr = strchr(ptr, ','))) {
    *chr = 0;
  }
  cfg_bookmark_alias[num] = ptr;

  cfg_bookmark_port[num] = port;
}


int addbookmark(char *line) {
  char alias[256], hostname[256];
  int port;

  if (cfg_numbookmarks >= 40) {
    printf("Too many bookmarks\n");
    return(0);
  }

  port = 23;

  // scanf is a piece of poop!
  if (sscanf(line, "bookmark = %255[^,] , %255[a-zA-Z0-9.-] , %d \n", alias, hostname, &port) == 3) {
  } else if (sscanf(line, "bookmark = %255[a-zA-Z0-9.-] , %d \n", hostname, &port) == 2) {
    snprintf(alias, sizeof(alias), "%s", hostname);
  } else if (sscanf(line, "bookmark = %255[^,] , %255[a-zA-Z0-9.-] \n", alias, hostname) == 2) {
  } else if (sscanf(line, "bookmark = %255[a-zA-Z0-9.-] \n", hostname) == 1) {
    snprintf(alias, sizeof(alias), "%s", hostname);
  } else {
    return(0);
  }

  /* Validate parsed data before using */
  if (!validate_hostname(hostname)) {
    printf("Invalid hostname in config: %s\n", hostname);
    return(0);
  }

  if (!validate_alias(alias)) {
    printf("Invalid alias in config: %s\n", alias);
    return(0);
  }

  if (port <= 0 || port > 65535) {
    printf("Invalid port in config: %d\n", port);
    return(0);
  }

  addhost(cfg_numbookmarks, alias, hostname, port);

  /* Check for optional mode field: "bookmark = alias, host, port, ansi" */
  cfg_bookmark_termmode[cfg_numbookmarks] = 0;  /* default: PETSCII */
  {
    char *p = line;
    int commas = 0;
    while (*p) {
      if (*p == ',') commas++;
      if (commas == 3) {
        p++;
        while (*p == ' ') p++;
        if (strncmp(p, "ansi", 4) == 0) {
          cfg_bookmark_termmode[cfg_numbookmarks] = 1;
        }
        break;
      }
      p++;
    }
  }

  ++cfg_numbookmarks;
  return(1);
}


signed int cfg_readconfig(char *configfile) {
  FILE *cfg;
  char linebuf[256], key[16], value[256];
  int line = 0;

  if ((cfg = fopen(configfile, "r")) == NULL) {
    return(0);
  } else {
    cfg_read = 1;
  }

  while (fgets(linebuf, sizeof(linebuf), cfg) != NULL) {

    if (strlen(linebuf) >= sizeof(linebuf) - 1) {
      printf("Line %d in %s is too long\n", line, configfile);
      fclose(cfg);
      return(-1);
    }

    if (linebuf[0] == '#') {
    } else if (strlen(linebuf) >= 3) {

      if (sscanf(linebuf, "%15s = %255s \n", key, value) == 2) {

	if (strcmp(key, "host") == 0) {
	  if (strchr(value, '.')) {
	    cfg_sethost(value);
	  } else {
	    printf("Invalid hostname in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "port") == 0) {
	  cfg_port = strtol(value, (char **)NULL, 10);
	  if (cfg_port <= 0 || cfg_port > 65535) {
	    printf("Invalid port number in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "senddelay") == 0) {
	  cfg_senddelay = strtol(value, (char **)NULL, 10);
	  if (cfg_senddelay < 0 || cfg_senddelay > 10000) {
	    printf("Invalid send delay in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "receivedelay") == 0) {
	  cfg_recvdelay = strtol(value, (char **)NULL, 10);
	  if (cfg_recvdelay < 0 || cfg_recvdelay > 10000) {
	    printf("Invalid receive delay in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "keyboard") == 0) {
          if (path_is_absolute(value)) {
            strncpy(keyboard, value, sizeof(keyboard) - 1);
            keyboard[sizeof(keyboard) - 1] = 0;
          } else {
            path_build_asset(keyboard, sizeof(keyboard), value);
          }
	  cfg_keyboard = keyboard;
	} else if (strcmp(key, "logfile") == 0) {
	  snprintf(logfile, sizeof(logfile), "%s", value);
	  cfg_logfile = logfile;
	} else if (strcmp(key, "xferdir") == 0) {
	  cfg_change_dir(cfg_xferdir, value);
	} else if (strcmp(key, "dldir") == 0) {
	  cfg_change_dir(cfg_dldir, value);
	} else if (strcmp(key, "seqdir") == 0) {
	  cfg_change_dir(cfg_seqdir, value);
	} else if (strcmp(key, "screendir") == 0) {
	  cfg_change_dir(cfg_screendir, value);
	} else if (strcmp(key, "localecho") == 0) {
	  if (strcmp("yes", value) == 0) {
	    cfg_localecho = 1;
	  } else if (strcmp("no", value) == 0) {
	    cfg_localecho = 0;
	  } else {
	    printf("Invalid localecho value in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "fullscreen") == 0) {
	  if (strcmp("yes", value) == 0) {
	    cfg_fullscreen = 1;
	  } else if (strcmp("no", value) == 0) {
	    cfg_fullscreen = 0;
	  } else {
	    printf("Invalid fullscreen value in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "sound") == 0) {
	  if (strcmp("yes", value) == 0) {
	    cfg_sound = 1;
	  } else if (strcmp("no", value) == 0) {
	    cfg_sound = 0;
	  } else {
	    printf("Invalid sound value in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
        
    } else if (strcmp(key, "debug") == 0) {
        if (strcmp("yes", value) == 0) {
            cfg_debugmode = 1;
        } else if (strcmp("no", value) == 0) {
            cfg_debugmode = 0;
        } else {
            printf("Invalid debug mode value in %s: %s\n", configfile, value);
            fclose(cfg);
            return(-1);
        }

    } else if (strcmp(key, "splash") == 0) {
        if (strcmp("yes", value) == 0) {
            cfg_splash = 1;
        } else if (strcmp("no", value) == 0) {
            cfg_splash = 0;
        } else {
            printf("Invalid splash value in %s: %s\n", configfile, value);
            fclose(cfg);
            return(-1);
        }

    } else if (strcmp(key, "splashfont") == 0) {
        cfg_splashfont = strtol(value, (char **)NULL, 10);
        if (cfg_splashfont < 0 || cfg_splashfont > 20) cfg_splashfont = 0;

    } else if (strcmp(key, "menufont") == 0) {
        cfg_menufont = strtol(value, (char **)NULL, 10);
        if (cfg_menufont < 0 || cfg_menufont > 20) cfg_menufont = 0;

    } else if (strcmp(key, "charset") == 0) {
        if (strcmp("swedish", value) == 0 || strcmp("se", value) == 0) {
            cfg_charset = 1;
        } else if (strcmp("german", value) == 0 || strcmp("de", value) == 0) {
            cfg_charset = 2;
        } else {
            cfg_charset = 0;
        }

    } else if (strcmp(key, "modem") == 0) {
        if (strcmp("yes", value) == 0) {
            cfg_modem = 1;
        } else if (strcmp("no", value) == 0) {
            cfg_modem = 0;
        }

	} else if (strcmp(key, "zoom") == 0) {
	  cfg_zoom = strtol(value, (char **)NULL, 10);
	  if (cfg_zoom <= 0 || cfg_zoom > 8) {
	    printf("Invalid zoom value in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "reconnect") == 0) {
	  cfg_reconnect = strtol(value, (char **)NULL, 10);
	  if (cfg_reconnect <= 0 || cfg_reconnect > 10000) {
	    printf("Invalid reconnect delay in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "columns") == 0) {
	  cfg_columns = strtol(value, (char **)NULL, 10);
	  if (cfg_columns != 40 && cfg_columns != 80) {
	    printf("Invalid number of columns in %s: %s\n", configfile, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "bookmarkfile") == 0) {
	  snprintf(cfg_bookmarkfile, sizeof(cfg_bookmarkfile), "%s", value);
	} else if (strcmp(key, "bookmark") == 0) {
	  if (addbookmark(linebuf) == 0) {
	    printf("Syntax error in %s line %d\n", configfile, line + 1);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "case") == 0) {
	  if (strcmp(value, "upper") == 0) {
	    gfx_setfont(0);
	  } else if (strcmp(value, "lower") == 0) {
	    gfx_setfont(1);
	  } else {
	    printf("Invalid case value in %s line %d: %s\n", configfile, line + 1, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else if (strcmp(key, "termmode") == 0) {
	  if (strcmp(value, "ansi") == 0) {
	    cfg_termmode = 1;
	  } else if (strcmp(value, "petscii") == 0) {
	    cfg_termmode = 0;
	  } else {
	    printf("Invalid termmode value in %s line %d: %s\n", configfile, line + 1, value);
	    fclose(cfg);
	    return(-1);
	  }
	} else {
	  printf("Unknown config key in %s line %d\n", configfile, line + 1);
	  fclose(cfg);
	  return(-1);
	}

      } else {
	printf("Error in %s line %d\n", configfile, line + 1);
	fclose(cfg);
	return(-1);
      }

    } else {

      if (!sscanf(linebuf, "%s", linebuf)) {
	printf("Syntax error in %s line %d\n", configfile, line + 1);
	fclose(cfg);
	return(-1);
      }

    }

    ++line;
  }

  if (ferror(cfg)) {
    printf("read error\n");
    fclose(cfg);
    return(-1);
  }


  fclose(cfg);
  return(0);
}


void cfg_writeconfig(char **data, char *configfile) {
  FILE *cfg;

  if ((cfg = fopen(configfile, "w")) == NULL) {
    return;
  }

  while (*data) {
    fprintf(cfg, "%s\n", *data++);
  }

  fclose(cfg);
}

/* Get the config file path */
static void cfg_get_config_path(char *out, size_t outsz) {
#ifdef WINDOWS
  snprintf(out, outsz, "cgterm.cfg");
#else
  snprintf(out, outsz, "%s/.cgtermrc", cfg_homedir);
#endif
}


/* Save a single setting to the config file.
 * If the key already exists, update it in place.
 * If not, append it at the end. */
void cfg_save_setting(const char *key, const char *value) {
  char fname[512];
  char tmpname[512];
  char linebuf[1024];
  FILE *in, *out;
  int found = 0;
  int keylen = (int)strlen(key);

  cfg_get_config_path(fname, sizeof(fname));
  snprintf(tmpname, sizeof(tmpname), "%s.tmp", fname);

  in = fopen(fname, "r");
  out = fopen(tmpname, "w");
  if (!out) return;

  if (in) {
    while (fgets(linebuf, sizeof(linebuf), in)) {
      /* Check if this line has our key */
      char *p = linebuf;
      while (*p == ' ' || *p == '\t') p++;
      if (*p != '#' && strncmp(p, key, keylen) == 0) {
        char *eq = p + keylen;
        while (*eq == ' ' || *eq == '\t') eq++;
        if (*eq == '=') {
          /* Found it — write updated value */
          fprintf(out, "%s = %s\n", key, value);
          found = 1;
          continue;
        }
      }
      fputs(linebuf, out);
    }
    fclose(in);
  }

  if (!found) {
    fprintf(out, "%s = %s\n", key, value);
  }
  fclose(out);

  /* Replace original with temp */
  remove(fname);
  rename(tmpname, fname);
}


void cfg_disable_splash(void) {
  cfg_splash = 0;
  cfg_save_setting("splash", "no");
}

void cfg_debug(const char *s){
    //Prints a debug message to output
    if(cfg_debugmode == 1){
        puts(s);
    }
}


static void cfg_resolve_bookmarkfile(char *resolved, size_t size) {
  if (cfg_bookmarkfile[0] == '~') {
#ifdef WINDOWS
    if (cfg_bookmarkfile[1] == '/' || cfg_bookmarkfile[1] == '\\') {
      snprintf(resolved, size, "%s\\%s", cfg_homedir, cfg_bookmarkfile + 2);
    } else {
      snprintf(resolved, size, "%s", cfg_bookmarkfile);
    }
#else
    if (cfg_bookmarkfile[1] == '/') {
      snprintf(resolved, size, "%s%s", cfg_homedir, cfg_bookmarkfile + 1);
    } else {
      snprintf(resolved, size, "%s", cfg_bookmarkfile);
    }
#endif
  } else {
    snprintf(resolved, size, "%s", cfg_bookmarkfile);
  }
}


void cfg_load_bookmarks(void) {
  FILE *bf;
  char linebuf[256];
  char resolved[512];

  cfg_resolve_bookmarkfile(resolved, sizeof(resolved));

  if ((bf = fopen(resolved, "r")) == NULL) {
    return;
  }

  while (fgets(linebuf, sizeof(linebuf), bf) != NULL) {
    if (linebuf[0] == '#' || strlen(linebuf) < 3) {
      continue;
    }
    addbookmark(linebuf);
  }

  fclose(bf);
}


void cfg_save_bookmark(char *alias, char *host, int port) {
  FILE *bf;
  char resolved[512];

  cfg_resolve_bookmarkfile(resolved, sizeof(resolved));

  if ((bf = fopen(resolved, "a")) != NULL) {
    if (cfg_termmode == 1) {
      fprintf(bf, "bookmark = %s, %s, %d, ansi\n", alias, host, port);
    } else {
      fprintf(bf, "bookmark = %s, %s, %d\n", alias, host, port);
    }
    fclose(bf);
  }
}


/* ---- Bookmark notes ----
 * Stored in ~/.cgterm-notes as:
 *   [host:port]
 *   line 1 of note
 *   line 2 of note
 *   [next_host:port]
 *   ...
 */

static char notes_path[512];
static int notes_path_inited = 0;

static void cfg_notes_path(void) {
  if (!notes_path_inited) {
#ifdef WINDOWS
    snprintf(notes_path, sizeof(notes_path), "cgterm-notes.cfg");
#else
    snprintf(notes_path, sizeof(notes_path), "%s/.cgterm-notes", cfg_homedir);
#endif
    notes_path_inited = 1;
  }
}

/* Read note for a given host:port. Returns static buffer or "" if none. */
static char note_buf[2048];

const char *cfg_get_bookmark_note(const char *host, int port) {
  FILE *f;
  char key[256], line[256];
  int found = 0;

  cfg_notes_path();
  note_buf[0] = 0;

  snprintf(key, sizeof(key), "[%s:%d]", host, port);

  if ((f = fopen(notes_path, "r")) == NULL) return note_buf;

  while (fgets(line, sizeof(line), f)) {
    /* Strip trailing newline */
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;

    if (line[0] == '[') {
      if (found) break;  /* hit next section, done */
      if (strcmp(line, key) == 0) found = 1;
    } else if (found) {
      if (strlen(note_buf) + strlen(line) + 2 < sizeof(note_buf)) {
        if (note_buf[0]) strcat(note_buf, "\n");
        strcat(note_buf, line);
      }
    }
  }
  fclose(f);
  return note_buf;
}


/* Save/overwrite note for a given host:port */
void cfg_set_bookmark_note(const char *host, int port, const char *note) {
  FILE *f, *tmp;
  char key[256], line[256];
  char tmppath[520];
  int skipping = 0;

  cfg_notes_path();
  snprintf(key, sizeof(key), "[%s:%d]", host, port);
  snprintf(tmppath, sizeof(tmppath), "%s.tmp", notes_path);

  tmp = fopen(tmppath, "w");
  if (!tmp) return;

  /* Copy existing notes, skipping the one we're replacing */
  f = fopen(notes_path, "r");
  if (f) {
    while (fgets(line, sizeof(line), f)) {
      if (line[0] == '[') {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (strcmp(line, key) == 0) {
          skipping = 1;
          continue;
        } else {
          skipping = 0;
        }
        fprintf(tmp, "%s\n", line);
      } else if (!skipping) {
        fputs(line, tmp);
      }
    }
    fclose(f);
  }

  /* Append new note */
  if (note && note[0]) {
    fprintf(tmp, "%s\n%s\n", key, note);
  }

  fclose(tmp);
#ifdef WINDOWS
  remove(notes_path);  /* Windows rename() fails if dest exists */
#endif
  rename(tmppath, notes_path);
}


void cfg_log_connection(const char *host, int port) {
  FILE *hf;
  char fname[512];
  time_t now;
  struct tm *tm_info;

#ifdef WINDOWS
  snprintf(fname, sizeof(fname), "cgterm-history.log");
#else
  snprintf(fname, sizeof(fname), "%s/.cgterm-history", cfg_homedir);
#endif

  now = time(NULL);
  tm_info = localtime(&now);

  if ((hf = fopen(fname, "a")) != NULL) {
    fprintf(hf, "%04d-%02d-%02d %02d:%02d:%02d  %s:%d\n",
            tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            host, port);
    fclose(hf);
  }
}
