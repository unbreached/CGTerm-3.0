#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL.h>
#include "sound.h"
#include "music.h"
#include "kernal.h"
#include "net.h"
#include "config.h"
#include "timer.h"
#include "gfx.h"
#include "modem.h"

#define SAMPLE_RATE 22050
#define AMPLITUDE   14000
#define PI 3.14159265358979323846


/* ---- Simple RNG for typo simulation ---- */
static unsigned int modem_rng = 0;
static unsigned int modem_rand(void) {
  modem_rng ^= modem_rng << 13;
  modem_rng ^= modem_rng >> 17;
  modem_rng ^= modem_rng << 5;
  return modem_rng;
}


/* ---- Typing simulation ---- */

/* Type a single PETSCII character with screen update */
static void type_char(int ch) {
  ffd2(ch);
  gfx_vbl();
}

/* Type a string one character at a time with random delays.
 * Returns 1 if ESC was pressed (abort), 0 otherwise. */
static int type_string(const char *text, int min_delay, int max_delay) {
  SDL_Event ev;
  while (*text) {
    int delay = min_delay + (int)(modem_rand() % (max_delay - min_delay + 1));
    type_char((unsigned char)*text);
    text++;
    /* Pump events during delay */
    {
      unsigned int deadline = timer_get_ticks() + delay;
      while (timer_get_ticks() < deadline) {
        while (SDL_PollEvent(&ev)) {
          if (ev.type == SDL_QUIT) exit(0);
          if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
            return 1;
        }
        timer_delay(5);
      }
    }
  }
  return 0;
}

/* Type a string with occasional typos and backspace corrections.
 * typo_chance: 1-in-N chance per character of making a typo.
 * Returns 1 if ESC pressed. */
static int type_string_with_typos(const char *text, int min_delay, int max_delay,
                                  int typo_chance) {
  SDL_Event ev;
  while (*text) {
    int delay = min_delay + (int)(modem_rand() % (max_delay - min_delay + 1));

    /* Maybe make a typo */
    if (typo_chance > 0 && (int)(modem_rand() % typo_chance) == 0 && *text != '\r') {
      /* Type a wrong character (nearby key) */
      char wrong = (char)('A' + (modem_rand() % 26));  /* uppercase = lowercase on C64 */
      type_char((unsigned char)wrong);

      /* Pause — realize the mistake */
      {
        unsigned int pause_end = timer_get_ticks() + 200 + (modem_rand() % 300);
        while (timer_get_ticks() < pause_end) {
          while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) exit(0);
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
              return 1;
          }
          timer_delay(5);
        }
      }

      /* Backspace to fix it */
      type_char(0x14);  /* PETSCII delete/backspace */
      {
        unsigned int pause_end = timer_get_ticks() + 80 + (modem_rand() % 120);
        while (timer_get_ticks() < pause_end) {
          while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) exit(0);
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
              return 1;
          }
          timer_delay(5);
        }
      }
    }

    /* Type the correct character */
    type_char((unsigned char)*text);
    text++;

    /* Delay between keystrokes */
    {
      unsigned int deadline = timer_get_ticks() + delay;
      while (timer_get_ticks() < deadline) {
        while (SDL_PollEvent(&ev)) {
          if (ev.type == SDL_QUIT) exit(0);
          if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
            return 1;
        }
        timer_delay(5);
      }
    }
  }
  return 0;
}


/* ---- Tone generation helpers ---- */

static int gen_tone(Sint16 *buf, int offset, int num_samples,
                    double freq1, double amp1,
                    double freq2, double amp2) {
  int i;
  for (i = 0; i < num_samples; i++) {
    double t = (double)(offset + i) / SAMPLE_RATE;
    double val = amp1 * sin(2.0 * PI * freq1 * t);
    if (freq2 > 0)
      val += amp2 * sin(2.0 * PI * freq2 * t);
    if (val > 32000) val = 32000;
    if (val < -32000) val = -32000;
    buf[offset + i] = (Sint16)val;
  }
  return offset + num_samples;
}

static int gen_silence(Sint16 *buf, int offset, int num_samples) {
  memset(buf + offset, 0, num_samples * sizeof(Sint16));
  return offset + num_samples;
}


/* ---- Pseudo-random noise generator for QAM simulation ---- */
static unsigned int carrier_rng = 0x12345678;
static double carrier_noise(void) {
  /* XorShift32 -> map to -1.0 .. +1.0 */
  carrier_rng ^= carrier_rng << 13;
  carrier_rng ^= carrier_rng >> 17;
  carrier_rng ^= carrier_rng << 5;
  return ((double)(carrier_rng & 0xFFFF) / 32768.0) - 1.0;
}

/* DTMF frequency table: index 0-9 for digits '0'-'9' */
static const double dtmf_low[]  = {941, 697, 697, 697, 770, 770, 770, 852, 852, 852};
static const double dtmf_high[] = {1336, 1209, 1336, 1477, 1209, 1336, 1477, 1209, 1336, 1477};

/* Generate one DTMF digit tone + gap */
static int gen_dtmf_digit(Sint16 *buf, int offset, char digit) {
  int tone_samples = SAMPLE_RATE / 10;    /* 100ms tone */
  int gap_samples = SAMPLE_RATE / 15;     /* ~67ms gap */
  if (digit >= '0' && digit <= '9') {
    int idx = digit - '0';
    offset = gen_tone(buf, offset, tone_samples,
                      dtmf_low[idx], AMPLITUDE * 0.45,
                      dtmf_high[idx], AMPLITUDE * 0.45);
    offset = gen_silence(buf, offset, gap_samples);
  }
  return offset;
}

/* Derive a deterministic fake phone number from hostname.
 * e.g. "bbs.retrohack.se" -> "512-738-4291" */
static void hostname_to_phone(const char *host, char *phone, int maxlen) {
  unsigned long hash = 5381;
  const char *p = host;
  while (*p) { hash = hash * 33 + (unsigned char)*p; p++; }
  snprintf(phone, maxlen, "%03d-%03d-%04d",
           200 + (int)(hash % 800),
           200 + (int)((hash / 800) % 800),
           (int)((hash / 640000) % 10000));
}


/* ---- Realistic V.34 carrier/handshake ---- */
/* Based on ITU V.8/V.34 specification phases:
 *   1. CNG tone (1100 Hz calling tone)
 *   2. CED/ANSam (2100 Hz answer tone with phase reversals)
 *   3. V.8 negotiation (V.21 FSK modulation ~1080-1850 Hz)
 *   4. Line probing (frequency sweep across 150-3750 Hz)
 *   5. Equalizer training (THE SCREECH — band-limited QAM noise at 1800 Hz)
 *   6. Final sync and speaker mute
 */
static int gen_carrier(Sint16 *buf, int offset, int num_samples) {
  int i;
  double total_dur = (double)num_samples / SAMPLE_RATE;

  /* Accumulating phase for FM synthesis (prevents clicks) */
  double phase1 = 0, phase2 = 0, phase3 = 0;
  double noise_phase = 0;

  carrier_rng = 0xDEADBEEF;  /* deterministic seed */

  for (i = 0; i < num_samples; i++) {
    double t = (double)i / SAMPLE_RATE;
    double pos = t / total_dur;  /* 0.0 to 1.0 progress */
    double val = 0;
    double dt = 1.0 / SAMPLE_RATE;

    /* Phase 1 (0-5%): CNG calling tone — 1100 Hz (~0.35s) */
    if (pos < 0.05) {
      double fade = (pos < 0.01) ? pos / 0.01 : 1.0;
      phase1 += 2.0 * PI * 1100.0 * dt;
      val = AMPLITUDE * 0.65 * fade * sin(phase1);
    }

    /* Phase 2 (5-18%): Answer tone — 2100 Hz with phase reversals (~0.9s) */
    else if (pos < 0.18) {
      double sub_t = t - total_dur * 0.05;
      int reversal_count = (int)(sub_t / 0.45);
      double sign = (reversal_count % 2 == 0) ? 1.0 : -1.0;
      phase1 += 2.0 * PI * 2100.0 * dt;
      val = AMPLITUDE * 0.75 * sign * sin(phase1);
    }

    /* Phase 3 (18-25%): V.8 negotiation — V.21 FSK warble (~0.5s) */
    else if (pos < 0.25) {
      /* V.21 channel 2: mark=1650 Hz, space=1850 Hz
       * Switching at ~300 baud (data-like pattern) */
      double sub_t = t - total_dur * 0.18;
      double bit_sel = sin(2.0 * PI * 150.0 * sub_t);
      double freq_v = (bit_sel > 0) ? 1650.0 : 1850.0;
      phase2 += 2.0 * PI * freq_v * dt;
      {
        double bit_sel2 = sin(2.0 * PI * 130.0 * sub_t + 1.0);
        double freq2 = (bit_sel2 > 0) ? 1080.0 : 1180.0;
        phase3 += 2.0 * PI * freq2 * dt;
      }
      val = AMPLITUDE * 0.45 * (sin(phase2) * 0.6 + sin(phase3) * 0.4);
    }

    /* Phase 4 (25-35%): Line probing — sweep tones across 150-3750 Hz (~0.7s) */
    else if (pos < 0.35) {
      double sub_pos = (pos - 0.25) / 0.10;
      /* Sweep up then down */
      double sweep;
      if (sub_pos < 0.45) {
        sweep = 150.0 + (3750.0 - 150.0) * (sub_pos / 0.45);
      } else if (sub_pos < 0.55) {
        sweep = 3750.0;  /* hold briefly at top */
      } else {
        sweep = 3750.0 - (3750.0 - 150.0) * ((sub_pos - 0.55) / 0.45);
      }
      phase1 += 2.0 * PI * sweep * dt;
      /* Add second sweep at different rate for richness */
      double sweep2 = 3750.0 - sweep + 150.0;
      phase2 += 2.0 * PI * sweep2 * dt * 0.3;
      val = AMPLITUDE * 0.5 * (sin(phase1) * 0.7 + sin(phase2) * 0.3);
    }

    /* Phase 5 (35-90%): THE SCREECH — equalizer training. (~3.85s of pure screech!)
     * Band-limited pseudo-random QAM noise centered at 1800 Hz.
     * This is the iconic modem sound. */
    else if (pos < 0.90) {
      /* Generate pseudo-random BPSK/QAM-like signal:
       * carrier at 1800 Hz, modulated by pseudo-random symbols
       * at ~3429 baud (V.34 symbol rate). Band-limited to 300-3400 Hz. */
      double sub_t = t - total_dur * 0.35;
      double symbol_rate = 3429.0;
      double symbol_phase = fmod(sub_t * symbol_rate, 1.0);

      /* New random symbol at each symbol boundary */
      if (symbol_phase < dt * symbol_rate) {
        carrier_noise();  /* advance RNG */
      }

      /* QAM-like: random phase/amplitude modulation of 1800 Hz carrier */
      noise_phase += 2.0 * PI * 1800.0 * dt;
      {
        double n1 = carrier_noise();
        double n2 = carrier_noise();
        /* Shaped noise: band-pass around 1800 Hz by mixing with carrier */
        double signal = n1 * sin(noise_phase) + n2 * cos(noise_phase);
        /* Add some tonal components for that characteristic "singing" quality */
        double tonal = 0.15 * sin(2.0 * PI * 1200.0 * t + n1 * 2.0)
                     + 0.12 * sin(2.0 * PI * 2400.0 * t + n2 * 1.5);
        val = AMPLITUDE * 0.55 * (signal * 0.75 + tonal * 0.25);
      }
    }

    /* Phase 6 (90-100%): Final sync burst then fade to silence */
    else {
      double sub_pos = (pos - 0.90) / 0.10;
      double fade = (1.0 - sub_pos);
      fade = fade * fade;  /* quadratic fade for natural decay */
      noise_phase += 2.0 * PI * 1800.0 * dt;
      {
        double n1 = carrier_noise();
        double signal = n1 * sin(noise_phase);
        /* Brief scrambled burst fading out */
        val = AMPLITUDE * 0.45 * fade * signal;
      }
    }

    if (val > 32000) val = 32000;
    if (val < -32000) val = -32000;
    buf[offset + i] = (Sint16)val;
  }
  return offset + num_samples;
}


/* ---- Scanline CRT effect ---- */
static void modem_draw_scanlines(void) {
  /* Draw faint dark lines every other row on the menu surface
   * for a CRT monitor effect */
  SDL_Surface *scr = SDL_GetVideoSurface();
  if (scr) {
    int y;
    SDL_LockSurface(scr);
    for (y = 0; y < scr->h; y += 2) {
      /* Darken every other line by writing semi-dark pixels */
      Uint8 *row = (Uint8 *)scr->pixels + y * scr->pitch;
      int x;
      for (x = 0; x < scr->w * scr->format->BytesPerPixel; x += scr->format->BytesPerPixel) {
        /* Reduce each channel by ~25% */
        if (scr->format->BytesPerPixel == 4) {
          row[x]   = row[x] * 3 / 4;
          row[x+1] = row[x+1] * 3 / 4;
          row[x+2] = row[x+2] * 3 / 4;
        }
      }
    }
    SDL_UnlockSurface(scr);
  }
}


/* ---- Check for ESC during audio playback ---- */
static int pump_events_check_esc(void) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) exit(0);
    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
      return 1;
  }
  return 0;
}


/* ---- Main modem simulation ---- */

int modem_connect(const char *host, int port, void (*status)(int, char *)) {
  char atdt_addr[128];
  char phone[32];
  Sint16 *audio_buf;
  int sample_id;
  int offset;
  int total_samples;
  int ndigits, di;
  unsigned int play_start;
  int ring1_ms, ring2_ms;
  int aborted = 0;
  int showed_ring1 = 0, showed_ring2 = 0;
  int already_connected = 0;

  /* If modem simulation disabled, connect directly */
  if (!cfg_modem) {
    return net_connect(host, port, status);
  }

  /* Pre-flight happens AFTER the modem terminal is drawn,
   * but BEFORE the audio plays. See below after ATDT. */

  /* Seed RNG */
  modem_rng = (unsigned int)time(NULL) ^ timer_get_ticks();
  if (modem_rng == 0) modem_rng = 12345;

  /* Build the ATDT address: just hostname (no port, user said ignore it) */
  snprintf(atdt_addr, sizeof(atdt_addr), "%s", host);

  /* ---- OLDSKOOL TERMINAL LOOK ---- */
  gfx_bgcolor(COLOR_BLACK);
  ffd2(147);  /* clear screen */
  gfx_setcursxy(0, 0);
  gfx_vbl();
  modem_draw_scanlines();
  SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);

  /* ---- Status bar at bottom (reverse video, cyan on blue) ---- */
  {
    int si;
    gfx_setcursxy(0, 24);
    ffd2(0x9a);  /* light blue text */
    ffd2(0x12);  /* reverse on */
    print(" com1: 9600,8,n,1 ");
    {
      int pad = cfg_columns - 18 - 11;
      for (si = 0; si < pad; si++) ffd2(' ');
    }
    print("cgterm 3.0 ");
    ffd2(0x92);  /* reverse off */
    gfx_setcursxy(0, 0);
    gfx_vbl();
    modem_draw_scanlines();
    SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
  }

  /* ---- Rainbow title ---- */
  /* C = red, G = orange, T = yellow, E = green, R = cyan, M = blue */
  ffd2(0x1c); print("c");  /* red */
  ffd2(0x81); print("g");  /* orange */
  ffd2(0x9e); print("t");  /* yellow */
  ffd2(0x1e); print("e");  /* green */
  ffd2(0x9f); print("r");  /* cyan */
  ffd2(0x1f); print("m");  /* blue */
  ffd2(0x05); /* white */
  print(" terminal v3.0\x0d");

  /* Separator line — full width */
  {
    int si;
    ffd2(0x9a);  /* light blue */
    for (si = 0; si < cfg_columns; si++) ffd2('-');
    ffd2(0x0d);
  }
  gfx_vbl();
  modem_draw_scanlines();
  SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
  timer_delay(300);

  /* ---- Phase 1: ATZ (white text, human typing speed) ---- */
  ffd2(0x05);  /* white */
  if (type_string("atz", 100, 250)) goto aborted;
  type_char(0x0d);
  timer_delay(400 + modem_rand() % 300);

  /* Modem response: OK (green = modem responses) */
  ffd2(0x1e);  /* green */
  print("ok\x0d");
  gfx_vbl();
  timer_delay(300);

  /* AT init string (white = user typing) */
  ffd2(0x05);  /* white */
  if (type_string("ats0=0e1v1x4&c1&d2", 40, 120)) goto aborted;
  type_char(0x0d);
  timer_delay(300 + modem_rand() % 200);
  ffd2(0x1e);  /* green */
  print("ok\x0d");
  gfx_vbl();
  timer_delay(400 + modem_rand() % 300);

  /* ---- Animated Phone Book ---- */
  {
    int pb_count, pb_target, pb_cursor;
    int si;
    int pb_start_y;

    ffd2(0x9f);  /* cyan */
    print("\x0d");
    ffd2(0x12);  /* reverse on */
    print(" loading phonebook... ");
    ffd2(0x92);  /* reverse off */
    print("\x0d\x0d");
    gfx_vbl();
    modem_draw_scanlines();
    SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
    timer_delay(800);

    /* Show all bookmarks — scroll if more than fit on screen */
    pb_count = cfg_numbookmarks;
    if (pb_count < 1) pb_count = 1;
    {
      int max_visible = 14;  /* max rows visible at once */
      int visible = pb_count < max_visible ? pb_count : max_visible;

      /* Find which bookmark index we're connecting to */
      pb_target = 0;
      for (si = 0; si < cfg_numbookmarks; si++) {
        if (cfg_bookmark_host[si] && strcmp(cfg_bookmark_host[si], host) == 0) {
          pb_target = si;
          break;
        }
      }

      pb_start_y = gfx_cursy;

      /* Print initial visible list */
      for (si = 0; si < visible; si++) {
      char entry[40];
      const char *bname = (si < cfg_numbookmarks && cfg_bookmark_alias[si])
                          ? cfg_bookmark_alias[si] : "unknown";
      /* Format: " N. BBSNAME" — PETSCII uppercase = lowercase codes */
      {
        int ei;
        snprintf(entry, sizeof(entry), " %d. ", si + 1);
        ffd2(0x9a);  /* light blue for number */
        print(entry);
        /* Print name in white, convert to PETSCII */
        ffd2(0x05);  /* white */
        for (ei = 0; bname[ei] && ei < 28; ei++) {
          char c = bname[ei];
          if (c >= 'A' && c <= 'Z') c = c + 32;  /* uppercase → PETSCII lowercase */
          else if (c >= 'a' && c <= 'z') c = c - 32;  /* lowercase → PETSCII uppercase */
          ffd2((unsigned char)c);
        }
        ffd2(0x0d);
      }
      gfx_vbl();
      modem_draw_scanlines();
      SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
      timer_delay(100 + modem_rand() % 100);
    }
    print("\x0d");
    gfx_vbl();
    timer_delay(300);

    /* Helper macro to draw a bookmark entry line */
    #define DRAW_PB_ENTRY(idx, row, highlight_color) { \
      char _ent[10]; \
      const char *_bn = ((idx) < cfg_numbookmarks && cfg_bookmark_alias[(idx)]) \
                        ? cfg_bookmark_alias[(idx)] : "unknown"; \
      int _ei; \
      gfx_setcursxy(0, (row)); \
      if (highlight_color) { ffd2(0x12); ffd2(highlight_color); } \
      else { ffd2(0x9a); } \
      snprintf(_ent, sizeof(_ent), " %2d. ", (idx) + 1); \
      print(_ent); \
      if (!highlight_color) ffd2(0x05); \
      for (_ei = 0; _bn[_ei] && _ei < 28; _ei++) { \
        char _c = _bn[_ei]; \
        if (_c >= 'A' && _c <= 'Z') _c += 32; \
        else if (_c >= 'a' && _c <= 'z') _c -= 32; \
        ffd2((unsigned char)_c); \
      } \
      for (; _ei < 28; _ei++) ffd2(' '); \
      if (highlight_color) ffd2(0x92); \
    }

    /* Animate cursor — human-like: move down toward target, variable speed */
    pb_cursor = 0;
    {
      int scroll_offset = 0;
      int vis_row;

      /* Maybe start a few entries above target for realism */
      /* (human scrolls from top, or maybe from a random spot) */

      while (pb_cursor != pb_target) {
        /* Scroll to keep cursor visible */
        if (pb_cursor >= scroll_offset + visible) {
          scroll_offset = pb_cursor - visible + 1;
          for (vis_row = 0; vis_row < visible; vis_row++) {
            int idx = scroll_offset + vis_row;
            if (idx < pb_count)
              DRAW_PB_ENTRY(idx, pb_start_y + vis_row, 0);
          }
        }

        /* Draw highlight */
        vis_row = pb_cursor - scroll_offset;
        DRAW_PB_ENTRY(pb_cursor, pb_start_y + vis_row, 0x9e);

        gfx_vbl();
        modem_draw_scanlines();
        SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);

        /* Human-like delay: varies between keystrokes.
         * Sometimes quick, sometimes a brief pause (reading the name) */
        {
          int base_delay = 150 + (int)(modem_rand() % 200);
          /* Occasionally pause longer — "hmm, is this the one?" */
          if ((modem_rand() % 5) == 0)
            base_delay += 300 + (int)(modem_rand() % 400);
          /* Slow down when getting close to target */
          if (pb_target - pb_cursor <= 2)
            base_delay += 200;
          timer_delay(base_delay);
        }

        /* Un-highlight */
        DRAW_PB_ENTRY(pb_cursor, pb_start_y + vis_row, 0);

        /* Move toward target */
        if (pb_cursor < pb_target)
          pb_cursor++;
        else
          pb_cursor--;

        if (pump_events_check_esc()) goto aborted;
      }
    }

    /* Final selection — highlight the target in green */
    {
      int final_row = pb_target - (pb_target >= visible ? pb_target - visible + 1 : 0);
      DRAW_PB_ENTRY(pb_target, pb_start_y + final_row, 0x1e); /* green highlight */
    }
    gfx_vbl();
    modem_draw_scanlines();
    SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
    timer_delay(600);

    /* Position cursor below the visible list */
    {
      int below = pb_start_y + (pb_count < visible ? pb_count : visible) + 1;
      if (below > 22) below = 22;
      gfx_setcursxy(0, below);
    }
    ffd2(0x9e);  /* yellow */
    print(" >> dialing...\x0d\x0d");
    gfx_vbl();
    modem_draw_scanlines();
    SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
    timer_delay(500);

    #undef DRAW_PB_ENTRY
    } /* end visible block */
  }

  /* ---- Phase 2: ATDT command (white = user typing, slower) ---- */
  ffd2(0x05);  /* white */
  if (type_string("atdt ", 80, 200)) goto aborted;

  /* hostname — uppercase in code = lowercase on C64 screen */
  {
    char upper_addr[128];
    int ai;
    strncpy(upper_addr, atdt_addr, sizeof(upper_addr) - 1);
    upper_addr[sizeof(upper_addr) - 1] = 0;
    for (ai = 0; upper_addr[ai]; ai++) {
      if (upper_addr[ai] >= 'a' && upper_addr[ai] <= 'z')
        upper_addr[ai] = upper_addr[ai] - 'a' + 'A';
    }
    if (type_string_with_typos(upper_addr, 60, 200, 10)) goto aborted;
  }

  type_char(0x0d);
  gfx_vbl();
  modem_draw_scanlines();
  SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);

  /* ---- Pre-flight: connect silently while ATDT is on screen ---- */
  {
    int preflight = net_connect(host, port, status);
    if (preflight != 0) {
      /* BBS is down */
      ffd2(0x1c);  /* red */
      print("\x0d no carrier\x0d");
      ffd2(0x05);
      gfx_vbl();
      modem_draw_scanlines();
      SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
      timer_delay(3000);
      /* Restore */
      gfx_bgcolor(COLOR_BLACK);
      gfx_fgcolor(COLOR_WHITE);
      ffd2(0x05);
      ffd2(147);
      gfx_vbl();
      return 1;
    }
    already_connected = 1;
  }

  /* ---- Phase 3: Generate and play modem audio ---- */

  /* Derive a fake phone number from hostname for DTMF tones */
  hostname_to_phone(host, phone, sizeof(phone));
  ndigits = 0;
  for (di = 0; phone[di]; di++) {
    if (phone[di] >= '0' && phone[di] <= '9') ndigits++;
  }

  /* Audio layout:
   * - dial tone:    1.0 sec
   * - DTMF digits:  ndigits * ~167ms
   * - pause:        0.5 sec
   * - ring 1:       2.0 sec tone + 2.0 sec silence
   * - ring 2:       2.0 sec tone + 0.8 sec silence
   * - carrier:      7.0 sec (extended for realistic negotiation)
   */
  total_samples = (int)(SAMPLE_RATE * 1.0)
                + ndigits * (SAMPLE_RATE / 10 + SAMPLE_RATE / 15)
                + (int)(SAMPLE_RATE * 0.5)
                + (int)(SAMPLE_RATE * 2.0) + (int)(SAMPLE_RATE * 2.0)
                + (int)(SAMPLE_RATE * 2.0) + (int)(SAMPLE_RATE * 0.8)
                + (int)(SAMPLE_RATE * 7.0);

  audio_buf = (Sint16 *)malloc(total_samples * sizeof(Sint16));
  if (!audio_buf) {
    return net_connect(host, port, status);
  }

  offset = 0;

  /* Dial tone: 350Hz + 440Hz */
  offset = gen_tone(audio_buf, offset, (int)(SAMPLE_RATE * 1.0),
                    350.0, AMPLITUDE * 0.35, 440.0, AMPLITUDE * 0.35);

  /* DTMF digits — play the fake phone number derived from hostname */
  for (di = 0; phone[di]; di++) {
    if (phone[di] >= '0' && phone[di] <= '9') {
      offset = gen_dtmf_digit(audio_buf, offset, phone[di]);
    }
  }

  /* Pause after dialing */
  offset = gen_silence(audio_buf, offset, (int)(SAMPLE_RATE * 0.5));

  /* Ring 1: 440Hz + 480Hz for 2 sec */
  ring1_ms = (offset * 1000) / SAMPLE_RATE;
  offset = gen_tone(audio_buf, offset, (int)(SAMPLE_RATE * 2.0),
                    440.0, AMPLITUDE * 0.3, 480.0, AMPLITUDE * 0.3);

  /* Silence between rings */
  offset = gen_silence(audio_buf, offset, (int)(SAMPLE_RATE * 2.0));

  /* Ring 2 */
  ring2_ms = (offset * 1000) / SAMPLE_RATE;
  offset = gen_tone(audio_buf, offset, (int)(SAMPLE_RATE * 2.0),
                    440.0, AMPLITUDE * 0.3, 480.0, AMPLITUDE * 0.3);

  /* Short silence before carrier */
  offset = gen_silence(audio_buf, offset, (int)(SAMPLE_RATE * 0.8));

  /* Carrier handshake: 7 seconds of V.34 negotiation */
  offset = gen_carrier(audio_buf, offset, (int)(SAMPLE_RATE * 7.0));

  total_samples = offset;

  /* Register and play — try SDL_mixer first, fall back to old system */
  sample_id = music_load_sfx_raw(audio_buf, total_samples * sizeof(Sint16));
  if (sample_id >= 0) {
    /* SDL_mixer path */
    music_play_sfx(sample_id);
  } else {
    /* Old sound system fallback */
    sample_id = sound_register_buffer((Uint8 *)audio_buf,
                                      total_samples * sizeof(Sint16));
    if (sample_id < 0) {
      free(audio_buf);
      return net_connect(host, port, status);
    }
    sound_play_sample(sample_id);
  }
  play_start = timer_get_ticks();

  /* ---- Phase 4: Sync loop — show RING... at right times ---- */
  while ((music_sfx_playing() || sound_is_playing()) && !aborted) {
    unsigned int elapsed_ms = timer_get_ticks() - play_start;

    if (pump_events_check_esc()) { aborted = 1; break; }

    if (!showed_ring1 && (int)elapsed_ms >= ring1_ms) {
      ffd2(0x1e);  /* green — modem response */
      print("ring\x0d");
      gfx_vbl();
      showed_ring1 = 1;
    }
    if (!showed_ring2 && (int)elapsed_ms >= ring2_ms) {
      ffd2(0x1e);  /* green */
      print("ring\x0d");
      gfx_vbl();
      showed_ring2 = 1;
    }

    gfx_vbl();
    modem_draw_scanlines();
    SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);
    timer_delay(20);
  }

  /* Cleanup audio */
  if (sample_id >= 0) {
    music_free_sfx(sample_id);
    /* Buffer is freed by us since music_load_sfx_raw doesn't own it */
  }
  free(audio_buf);

  if (aborted) {
    goto aborted;
  }

  /* ---- Phase 5: CONNECT 9600 ---- */
  timer_delay(200);
  ffd2(0x1e);  /* green — modem response */
  print("connect 9600\x0d\x0d");
  gfx_vbl();
  timer_delay(500);

  /* Separator */
  {
    int si;
    ffd2(0x9a);  /* light blue */
    for (si = 0; si < cfg_columns; si++) ffd2('-');
    ffd2(0x0d);
  }

  ffd2(0x9f);  /* cyan */
  print(" carrier detected - negotiating...\x0d");
  gfx_vbl();
  timer_delay(600);
  ffd2(0x1e);  /* green */
  print(" connection established.\x0d\x0d");
  gfx_vbl();
  timer_delay(800);

  ffd2(0x05);  /* white */
  print(" connecting to ");
  {
    char upper_h[64];
    int hi;
    snprintf(upper_h, sizeof(upper_h), "%s:%d", host, port);
    for (hi = 0; upper_h[hi]; hi++) {
      if (upper_h[hi] >= 'a' && upper_h[hi] <= 'z')
        upper_h[hi] = upper_h[hi] - 'a' + 'A';
    }
    print(upper_h);
  }
  print("...\x0d");
  gfx_vbl();
  modem_draw_scanlines();
  SDL_UpdateRect(SDL_GetVideoSurface(), 0, 0, 0, 0);

  /* Restore normal colors */
  gfx_bgcolor(COLOR_BLACK);
  gfx_fgcolor(COLOR_WHITE);
  ffd2(0x05);
  ffd2(147);  /* clear screen */
  gfx_vbl();

  /* Flush any stale SDL events from the modem sequence
   * (prevents phantom keypresses on Windows) */
  {
    SDL_Event flush_ev;
    while (SDL_PollEvent(&flush_ev)) { /* discard */ }
  }

  /* Already connected from pre-flight check */
  if (already_connected) {
    return 0;  /* success */
  }
  return net_connect(host, port, status);

aborted:
  ffd2(0x1c);  /* red */
  print("\x0d no carrier\x0d");
  ffd2(0x05);  /* white */
  gfx_vbl();
  return 1;
}
