#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "login.h"
#include "config.h"
#include "clipboard.h"
#include "timer.h"

#define LOGIN_WAIT_TIMEOUT 30000

static char script[1024];
static const char *step;       /* next unparsed step, NULL when idle */
static char wait_text[256];    /* w: target, "" when not waiting */
static int wait_len;
static char ring[256];         /* last received bytes, ASCII-normalised */
static int ring_len;
static unsigned int deadline;  /* w: / p: expiry */
static int pausing;

/* PETSCII -> ASCII letters so "w:Password" matches whatever case the board
 * uses: 0x41-0x5A is lower case on a C64, 0xC1-0xDA upper case. Everything
 * is folded to lower case for the comparison. */
static char normalise(int c) {
  c &= 0xFF;
  if (c >= 0xC1 && c <= 0xDA) c -= 0x80;
  if (c >= 'A' && c <= 'Z') c += 32;
  return (char)c;
}

static void run_next_step(void);

/* Find wait_text anywhere in the ring (the prompt may have arrived during an
 * earlier step, followed by a space or a cursor code). On a hit the ring is
 * cut after the match so the same prompt cannot satisfy a later step. */
static int ring_has_wait_text(void) {
  int i;
  if (wait_len == 0 || ring_len < wait_len) return 0;
  for (i = 0; i + wait_len <= ring_len; i++) {
    if (memcmp(ring + i, wait_text, wait_len) == 0) {
      int keep = ring_len - (i + wait_len);
      memmove(ring, ring + i + wait_len, keep);
      ring_len = keep;
      return 1;
    }
  }
  return 0;
}

void login_abort(void) {
  step = NULL;
  wait_text[0] = 0;
  wait_len = 0;
  pausing = 0;
}

int login_active(void) {
  return step != NULL;
}

void login_start(const char *host, int port) {
  const char *s = cfg_get_bookmark_login(host, port);

  login_abort();
  if (!s || !s[0]) {
    return;
  }
  snprintf(script, sizeof(script), "%s", s);
  step = script;
  ring_len = 0;
  printf("Login script started for %s:%d\n", host, port);
  run_next_step();
}

/* Parse and start the next step. Send steps are queued and the script
 * waits for the paste queue to drain before continuing. */
static void run_next_step(void) {
  char tok[300];
  const char *end;
  size_t len;

  while (step) {
    while (*step == ';' || *step == ' ') step++;
    if (!*step) {
      step = NULL;
      printf("Login script finished\n");
      return;
    }
    end = strchr(step, ';');
    len = end ? (size_t)(end - step) : strlen(step);
    if (len >= sizeof(tok)) len = sizeof(tok) - 1;
    memcpy(tok, step, len);
    tok[len] = 0;
    step = end ? end + 1 : step + len;

    if (strncmp(tok, "w:", 2) == 0) {
      const char *t = tok + 2;
      size_t i;
      wait_len = 0;
      for (i = 0; t[i] && wait_len < (int)sizeof(wait_text) - 1; i++) {
        wait_text[wait_len++] = normalise((unsigned char)t[i]);
      }
      wait_text[wait_len] = 0;
      if (wait_len == 0) continue;
      deadline = timer_get_ticks() + LOGIN_WAIT_TIMEOUT;
      /* the text may already be on screen from before this step started */
      if (ring_has_wait_text()) {
        wait_text[0] = 0;
        wait_len = 0;
        continue;
      }
      return;
    } else if (strncmp(tok, "s:", 2) == 0) {
      clipboard_queue_text(tok + 2);
      return;                     /* resume when the queue has drained */
    } else if (strcmp(tok, "cr") == 0) {
      clipboard_queue_text("\r");
      return;
    } else if (strncmp(tok, "p:", 2) == 0) {
      deadline = timer_get_ticks() + (unsigned int)strtol(tok + 2, NULL, 10);
      pausing = 1;
      return;
    } else {
      printf("Login script: unknown step '%s' - aborted\n", tok);
      login_abort();
      return;
    }
  }
}

int login_tick(int c) {
  if (!step && !wait_len && !pausing) {
    return 0;
  }
  if (c >= 0 && ring_len < (int)sizeof(ring)) {
    ring[ring_len++] = normalise(c);
  } else if (c >= 0) {
    memmove(ring, ring + 1, sizeof(ring) - 1);
    ring[sizeof(ring) - 1] = normalise(c);
  }

  if (wait_len) {
    if (ring_has_wait_text()) {
      wait_text[0] = 0;
      wait_len = 0;
      run_next_step();
    } else if (timer_get_ticks() > deadline) {
      printf("Login script: timed out waiting for '%s'\n", wait_text);
      login_abort();
    }
    return 1;
  }
  if (pausing) {
    if (timer_get_ticks() > deadline) {
      pausing = 0;
      run_next_step();
    }
    return 1;
  }
  if (!clipboard_paste_pending()) {
    run_next_step();           /* previous send step has drained */
  }
  return step != NULL || wait_len || pausing;
}
