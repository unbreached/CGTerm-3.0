/* Auto-login script engine (login.c) against a scripted board. */
#include <stdio.h>
#include <string.h>
#include "login.h"

/* ---- what login.c needs from the rest of the program ---- */
static const char *script_text = "";
const char *cfg_get_bookmark_login(const char *host, int port) { (void)host; (void)port; return script_text; }

static char sent[1024];           /* everything the script queued for the board */
static int pending = 0;           /* bytes "still draining" from the paste queue */
void clipboard_queue_text(const char *t) { strncat(sent, t, sizeof(sent) - strlen(sent) - 1); pending = 1; }
int clipboard_paste_pending(void) { return pending; }

static unsigned int vclock = 0;
unsigned int timer_get_ticks(void) { return vclock; }

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

static void start(const char *s) { script_text = s; sent[0] = 0; pending = 0; vclock = 0; login_start("board", 6400); }
/* the board prints text; the paste queue drains between bytes */
static void board(const char *text) {
  for (; *text; text++) { login_tick((unsigned char)*text); pending = 0; login_tick(-1); }
}
static void idle(unsigned int ms) { vclock += ms; pending = 0; login_tick(-1); }

int main(void) {
  puts("[login script]");
  start("w:handle;s:m00p;cr;w:password;s:secret;cr");
  CHECK(login_active() && sent[0] == 0, "nothing is sent before the prompt");
  board("Welcome!\rEnter your handle: ");
  idle(10);
  CHECK(strcmp(sent, "m00p\r") == 0, "handle and RETURN sent after the prompt");
  board("Password: ");
  idle(10); idle(10);
  CHECK(strcmp(sent, "m00p\rsecret\r") == 0, "password sent after its prompt");
  CHECK(!login_active(), "script finished");

  puts("[prompt matching]");
  start("w:password;s:x");
  board("\x05" "pA\x1cSS\x9eWO\x12RD\x92: ");        /* colour and reverse codes inside the word */
  idle(10);
  CHECK(strcmp(sent, "x") == 0, "colour and control codes inside the prompt are ignored");
  start("w:Password;s:x");
  board("\xd0\xc1\xd3\xd3\xd7\xcf\xd2\xc4");          /* PETSCII upper-case range */
  idle(10);
  CHECK(strcmp(sent, "x") == 0, "PETSCII upper-case prompt matches any case in the script");
  start("w:name;s:a;w:name;s:b");
  board("name: ");
  idle(10); idle(10);
  CHECK(strcmp(sent, "a") == 0, "one prompt satisfies one wait only");
  board("name: ");
  idle(10);
  CHECK(strcmp(sent, "ab") == 0, "the second wait needs the prompt again");

  puts("[escapes, pauses, errors]");
  start("s:pa\\;ss;cr");
  idle(10); idle(10);
  CHECK(strcmp(sent, "pa;ss\r") == 0, "\\; is a literal semicolon");
  start("p:500;s:late");
  idle(100);
  CHECK(sent[0] == 0, "pause holds the next step");
  idle(500);
  CHECK(strcmp(sent, "late") == 0, "and releases it after the time");
  start("p:999999999;s:never");
  idle(61000);
  CHECK(strcmp(sent, "never") == 0, "a huge pause is clamped to 60 s");
  start("w:nothing;s:x");
  board("some other text");
  idle(31000);
  CHECK(!login_active() && sent[0] == 0, "a wait times out after 30 s and sends nothing");
  start("s:one;bogus;s:two");
  idle(10); idle(10);
  CHECK(!login_active() && strcmp(sent, "one") == 0, "an unknown step aborts the script");
  start("w:a;s:secret");
  login_abort();
  board("a");
  idle(10);
  CHECK(sent[0] == 0 && !login_active(), "abort stops a waiting script (disconnect)");
  start("");
  CHECK(!login_active(), "no script, nothing to do");

  printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
  return fails != 0;
}
