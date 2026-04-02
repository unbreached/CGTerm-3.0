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


#ifdef WINDOWS
#define DIRCHAR '\\'
#else
#define DIRCHAR '/'
#endif


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
  const char *lang = getenv("LC_ALL");
  if (lang == NULL || !*lang) lang = getenv("LC_CTYPE");
  if (lang == NULL || !*lang) lang = getenv("LANG");

  int swedish = 0;
  if (lang != NULL) {
    if (strncasecmp(lang, "sv", 2) == 0 || strstr(lang, "_SE") != NULL || strstr(lang, "-SE") != NULL) {
      swedish = 1;
    }
  }

#ifdef WINDOWS
  return swedish ? "win-se-c64.kbd" : "win-us-c64.kbd";
#elif defined(__APPLE__)
  return swedish ? "mac-se-c64.kbd" : "mac-us-c64.kbd";
#else
  return swedish ? "linux-se-c64.kbd" : "linux-us-c64.kbd";
#endif
}

int cfg_init(char *argv0) {
#ifdef WINDOWS
  cfg_homedir = ".";
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
    dirbuffer[l++] = DIRCHAR;
    dirbuffer[l] = 0;
  }
  {
    size_t remaining = 256 - strlen(dirbuffer);
    strncat(dirbuffer, newdir, remaining - 1);
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
    printf("Malloc failed, prepare to crash\n"); // :P
  }
  strcpy(ptr, hostname);
  if ((chr = strchr(ptr, ','))) {
    *chr = 0;
  }
  cfg_bookmark_host[num] = ptr;

  if ((ptr = malloc(strlen(alias) + 1)) == NULL) {
    printf("Malloc failed, prepare to crash\n"); // :P
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

  addhost(cfg_numbookmarks, alias, hostname, port);
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

void cfg_disable_splash(void) {
    FILE *cf;
    char fname[512];

    cfg_splash = 0;

    /* Write the setting to config file so it persists */
#ifdef WINDOWS
    snprintf(fname, sizeof(fname), "cgterm.cfg");
#else
    snprintf(fname, sizeof(fname), "%s/.cgtermrc", cfg_homedir);
#endif

    /* Append splash = no to the config file */
    cf = fopen(fname, "a");
    if (cf) {
        fprintf(cf, "\nsplash = no\n");
        fclose(cf);
    }
}

void cfg_debug(const char *s){
    //Prints a debug message to output
    if(cfg_debugmode == 1){
        puts(s);
    }
}


static void cfg_resolve_bookmarkfile(char *resolved, size_t size) {
  if (cfg_bookmarkfile[0] == '~' && cfg_bookmarkfile[1] == '/') {
    snprintf(resolved, size, "%s%s", cfg_homedir, cfg_bookmarkfile + 1);
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
    fprintf(bf, "bookmark = %s, %s, %d\n", alias, host, port);
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
