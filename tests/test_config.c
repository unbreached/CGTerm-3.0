/* Config file parser (config.c): a bad file must never stop CGTerm from
 * starting, and what the program saves it must be able to read back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"

/* ---- what config.c needs from the rest of the program ---- */
void gfx_setfont(int f) { (void)f; }

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

static char dir[256], cfgpath[512];

static void write_cfg(const char *text) {
  FILE *f = fopen(cfgpath, "w");
  if (!f) { perror(cfgpath); exit(2); }
  fputs(text, f);
  fclose(f);
}
static int has_bookmark(const char *host, int port) {
  int i;
  for (i = 0; i < cfg_numbookmarks; i++)
    if (cfg_bookmark_host[i] && strcmp(cfg_bookmark_host[i], host) == 0 && cfg_bookmark_port[i] == port) return 1;
  return 0;
}

int main(void) {
  char longline[2100];
  char text[4096];
  struct stat st;
  FILE *f;
  int rc, i;

  snprintf(dir, sizeof dir, "/tmp/cgt-config-test-%d", (int)getpid());
  mkdir(dir, 0700);
  setenv("HOME", dir, 1);
  snprintf(cfgpath, sizeof cfgpath, "%s/.cgtermrc", dir);
  cfg_init("cgterm");

  puts("[bad lines are skipped, not fatal]");
  memset(longline, 'x', sizeof longline - 1); longline[sizeof longline - 1] = 0;
  snprintf(text, sizeof text,
           "zoom = 3\n"
           "nosuchkey = 1\n"
           "macro1 = %s\n"                 /* far too long */
           "zoom = 99\n"                   /* out of range: keeps 3 */
           "port = 70000\n"
           "columns = 123\n"
           "this line has no equals sign\n"
           "reconnect = 0\n"
           "localecho = maybe\n"
           "receivedelay = 5\n", longline);
  write_cfg(text);
  cfg_warnings = 0; cfg_warning[0] = 0;
  rc = cfg_readconfig(cfgpath);
  CHECK(rc == 0, "a file full of mistakes still loads");
  CHECK(cfg_zoom == 3, "a valid setting before the mistakes is applied, and a bad value does not replace it");
  CHECK(cfg_columns == 40, "an invalid column count keeps the old value");
  CHECK(cfg_port >= 1 && cfg_port <= 65535, "an invalid port keeps a valid one");
  CHECK(cfg_reconnect == 0, "reconnect = 0 is accepted");
  CHECK(cfg_recvdelay == 5, "settings after the mistakes are still read");
  CHECK(cfg_warnings >= 4 && cfg_warning[0] != 0, "the problems are counted and the first is kept for the screen");

  puts("[what CGTerm saves, it reads back]");
  write_cfg("zoom = 2\n");
  CHECK(cfg_save_setting("macro3", "hello world") != 0, "a macro is saved");
  memset(longline, 'm', 250); longline[250] = 0;
  CHECK(cfg_save_setting("macro4", longline) != 0, "a 250-character macro is saved");
  cfg_warnings = 0;
  rc = cfg_readconfig(cfgpath);
  CHECK(rc == 0 && cfg_warnings == 0, "and the file loads again without a warning");
  CHECK(strcmp(cfg_macros[3], "hello world") == 0, "macro text survives, spaces included");
  CHECK(strlen(cfg_macros[4]) == 250, "the long macro survives in full");
  CHECK(cfg_save_setting("zoom", "4") != 0, "an existing key is replaced, not duplicated");
  f = fopen(cfgpath, "r"); i = 0;
  while (f && fgets(text, sizeof text, f)) if (strncmp(text, "zoom", 4) == 0) i++;
  if (f) fclose(f);
  CHECK(i == 1, "one zoom line in the file");

  puts("[bookmarks]");
  write_cfg("bookmark = My Board, bbs.example.org, 6400\n"
            "bookmark = under_score.example.org, 23\n"
            "bookmark = V6, 2001:db8::1, 6400\n"
            "bookmark = Bad, host with spaces, 23\n"
            "bookmark = bad;host, 23\n");
  cfg_numbookmarks = 0;
  cfg_warnings = 0;
  rc = cfg_readconfig(cfgpath);
  CHECK(rc == 0, "bookmark file loads");
  CHECK(has_bookmark("bbs.example.org", 6400), "alias, host, port");
  CHECK(has_bookmark("under_score.example.org", 23), "underscore in a host name is kept");
  CHECK(has_bookmark("2001:db8::1", 6400), "IPv6 address is kept");
  CHECK(!has_bookmark("host with spaces", 23) && !has_bookmark("bad;host", 23), "malformed hosts are not loaded");
  for (i = 0; i < 60; i++) {
    snprintf(text, sizeof text, "bookmark = b%d.example.org, %d\n", i, 1000 + i);
    f = fopen(cfgpath, i ? "a" : "w"); fputs(text, f); fclose(f);
  }
  cfg_numbookmarks = 0;
  rc = cfg_readconfig(cfgpath);
  CHECK(rc == 0 && cfg_numbookmarks > 0 && cfg_numbookmarks <= 60, "more bookmarks than the list holds does not break loading");

  puts("[private files]");
  cfg_set_bookmark_note("bbs.example.org", 6400, "login=w:pass;s:secret;cr");
  cfg_user_file(text, sizeof text, "cgterm-notes");
  CHECK(strstr(cfg_get_bookmark_login("bbs.example.org", 6400), "s:secret") != NULL, "login script is stored and found again");
  if (stat(text, &st) == 0) {
    CHECK((st.st_mode & 077) == 0, "the notes file (auto-login passwords) is readable by the owner only");
  } else {
    printf("  note: notes file not at %s, permission check skipped\n", text);
  }
  cfg_log_connection("bbs.example.org", 6400);
  {
    CfgHistoryEntry h[4];
    int n = cfg_read_history(h, 4);
    CHECK(n == 1 && strcmp(h[0].host, "bbs.example.org") == 0 && h[0].port == 6400, "connection history round trip");
  }

  snprintf(text, sizeof text, "rm -rf '%s'", dir);
  if (strncmp(dir, "/tmp/cgt-config-test-", 21) == 0 && system(text) != 0) { /* leave it */ }
  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
