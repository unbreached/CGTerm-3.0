#include <stdio.h>
#include <SDL.h>
#include "gfx.h"
#include "keyboard.h"
#include "kernal.h"
#include "sound.h"


unsigned char screencode[256];
signed int scconv[] = {
  128,
  0,
  -64,
  -32,
  64,
  -64,
  -128,
  -128
};
unsigned char enableshiftcbm;
unsigned char rvson;
static unsigned char last_was_cr = 0;


int kernal_init(void) {
  int c;

  for (c = 0; c < 256; ++c) {
    screencode[c] = c + scconv[c / 32];
  }
  screencode[255] = 94;
  gfx_setcursxy(0, 0);
  enableshiftcbm = 1;
  rvson = 0;

  gfx_setfont(1);
  gfx_bgcolor(0);
  ffd2(154);
  ffd2(147);

  return(0);
}


void ffd2(unsigned char a) {

  /* Clear CR tracking for anything that isn't LF */
  if (a != 10) {
    last_was_cr = 0;
  }

  switch (a) {

    /* colors */

  case 5:
    gfx_fgcolor(COLOR_WHITE);
    break;

  case 28:
    gfx_fgcolor(COLOR_RED);
    break;

  case 30:
    gfx_fgcolor(COLOR_GREEN);
    break;

  case 31:
    gfx_fgcolor(COLOR_BLUE);
    break;

  case 129:
    gfx_fgcolor(COLOR_ORANGE);
    break;

  case 144:
    gfx_fgcolor(COLOR_BLACK);
    break;

  case 149:
    gfx_fgcolor(COLOR_BROWN);
    break;

  case 150:
    gfx_fgcolor(COLOR_LTRED);
    break;

  case 151:
    gfx_fgcolor(COLOR_DKGRAY);
    break;

  case 152:
    gfx_fgcolor(COLOR_GRAY);
    break;

  case 153:
    gfx_fgcolor(COLOR_LTGREEN);
    break;

  case 154:
    gfx_fgcolor(COLOR_LTBLUE);
    break;

  case 155:
    gfx_fgcolor(COLOR_LTGRAY);
    break;

  case 156:
    gfx_fgcolor(COLOR_PURPLE);
    break;

  case 158:
    gfx_fgcolor(COLOR_YELLOW);
    break;

  case 159:
    gfx_fgcolor(COLOR_CYAN);
    break;


    /* movement */

  case 10:
    /* LF after CR is a CR+LF pair — skip the LF since CR already moved down.
       Bare LF (without preceding CR) means move down without carriage return. */
    if (!last_was_cr) {
      gfx_cursdown();
    }
    last_was_cr = 0;
    return;

  case 13:
  case 141:
    gfx_setcursx(0);
    rvson = 0;
    last_was_cr = 1;
    /* fall through */

  case 17:
    gfx_cursdown();
    break;

  case 147:
    gfx_setcursxy(0, 0);
    gfx_cls();
    break;

  case 19:
    gfx_setcursxy(0, 0);
    break;

  case 20:
    gfx_delete();
    break;

  case 157:
    gfx_cursleft();
    break;

  case 29:
    gfx_cursright();
    break;

  case 145:
    gfx_cursup();
    break;

  case 148:
    gfx_insert();
    break;


    /* fonts */

  case 8:
    enableshiftcbm = 0;
    /* printf("shift+cbm disabled\n"); */
    break;

  case 9:
    enableshiftcbm = 1;
    /* printf("shift+cbm enabled\n"); */
    break;

  case 14:
    gfx_setfont(1);
    /* printf("switching to lower case\n"); */
    break;

  case 142:
    gfx_setfont(0);
    /* printf("switching to upper case\n"); */
    break;

  case 18:
    rvson = 1;
    break;

  case 146:
    rvson = 0;
    break;


    /* terminal */

  case 3:
    break;

  case 7:
    sound_play_sample(sound_bell);
    break;


    /* printables */

  default:
    if ((a >= 32 && a <= 127) || (a >= 160)) {
      gfx_draw_char(screencode[a] + rvson * 128);
      gfx_cursadvance();
    }
    break;

  }

}


void print(const char *s) {
  while (*s) {
    ffd2(*s++);
  }
}


void print_ascii(const unsigned char *s) {
  char c;

  while ((c = *s++)) {
    if (c >= 'A' && c <= 'Z') {
      c += 32;
    } else if (c >= 'a' && c <= 'z') {
      c -= 32;
    }
    ffd2(c);
  }
}
unsigned char ffe4(void) {
  int k = kbd_getkey();
  if (k < 0) {
    return 0;
  }
  return (unsigned char)k;
}
