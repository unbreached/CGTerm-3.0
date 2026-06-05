
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"
#include "config.h"
#include "keyboard.h"
#include "macro.h"
#include "ui.h"
#include "clipboard.h"
#include "net.h"


int focus_count = 0;
Focus focus_focus[10];
void (*focus_handler[10])(SDL_keysym *);

Focus kbd_focus;
static SDL_bool seqfile_open = SDL_FALSE;
static FILE *seqfile;
unsigned char keytable[SDLK_LAST][5];

typedef struct {
  const char *name;
  unsigned char unshifted;
  unsigned char shifted;
  unsigned char cbm;
  unsigned char control;
} SymbolicKeyMap;

// SDL KeyCode, Unshifted, Shifted, CBM, Control
static short DefaultKybdMapping[][5] = {
    {0x8,0x14,0x94,0x94,0x0},
    {0xD,0xD,0x8D,0x8D,0x0},
    {0x20,0x20,0xA0,0xA0,0x0},
    {0x27,0x27,0x22,0x0,0x0},
    {0x2A,0x2A,0xC0,0xDF,0x0},
    {0x2B,0x2B,0xDB,0xA6,0x0},
    {0x2C,0x2C,0x3C,0x3C,0x0},
    {0x2D,0x2D,0xDD,0xDC,0x0},
    {0x2E,0x2E,0x3E,0x3E,0x0},
    {0x2F,0x2F,0x3F,0x3F,0x0},
    {0x30,0x30,0x29,0x30,0x92},
    {0x31,0x31,0x21,0x81,0x90},
    {0x32,0x32,0x40,0x95,0x5},
    {0x33,0x33,0x23,0x96,0x1C},
    {0x34,0x34,0x24,0x97,0x9F},
    {0x35,0x35,0x25,0x98,0x9C},
    {0x36,0x36,0x5F,0x99,0x1E},
    {0x37,0x37,0x26,0x9A,0x1F},
    {0x38,0x38,0x2A,0x9B,0x9E},
    {0x39,0x39,0x28,0x29,0x12},
    {0x3A,0x3A,0x5B,0x5B,0x1B},
    {0x3B,0x3B,0x3A,0x5D,0x1D},
    {0x3D,0x3D,0x2B,0x0,0x0},
    {0x5B,0x5B,0xBA,0xA4,0x0},
    {0x5C,0x5E,0xDE,0xDE,0x0},
    {0x5D,0x5D,0xC0,0xDF,0x0},
    {0x60,0x5F,0x5F,0x5F,0x6},
    {0x61,0x41,0xC1,0xB0,0x1},
    {0x62,0x42,0xC2,0xBF,0x2},
    {0x63,0x43,0xC3,0xBC,0x3},
    {0x64,0x44,0xC4,0xAC,0x4},
    {0x65,0x45,0xC5,0xB1,0x5},
    {0x66,0x46,0xC6,0xBB,0x6},
    {0x67,0x47,0xC7,0xA5,0x7},
    {0x68,0x48,0xC8,0xB4,0x8},
    {0x69,0x49,0xC9,0xA2,0x9},
    {0x6A,0x4A,0xCA,0xB5,0xA},
    {0x6B,0x4B,0xCB,0xA1,0xB},
    {0x6C,0x4C,0xCC,0xB6,0xC},
    {0x6D,0x4D,0xCD,0xA7,0xD},
    {0x6E,0x4E,0xCE,0xAA,0xE},
    {0x6F,0x4F,0xCF,0xB9,0xF},
    {0x70,0x50,0xD0,0xAF,0x10},
    {0x71,0x51,0xD1,0xAB,0x11},
    {0x72,0x52,0xD2,0xB2,0x12},
    {0x73,0x53,0xD3,0xAE,0x13},
    {0x74,0x54,0xD4,0xA3,0x14},
    {0x75,0x55,0xD5,0xB8,0x15},
    {0x76,0x56,0xD6,0xBE,0x16},
    {0x77,0x57,0xD7,0xB3,0x17},
    {0x78,0x58,0xD8,0xBD,0x18},
    {0x79,0x59,0xD9,0xB7,0x19},
    {0x7A,0x5A,0xDA,0xAD,0x1A},
    {0x7F,0x5C,0xA9,0xA8,0x1C},
    {0x100,0x30,0x29,0x30,0x92},
    {0x101,0x31,0x21,0x81,0x90},
    {0x102,0x32,0x40,0x95,0x5},
    {0x103,0x33,0x23,0x96,0x1C},
    {0x104,0x34,0x24,0x97,0x9F},
    {0x105,0x35,0x25,0x98,0x9C},
    {0x106,0x36,0x5F,0x99,0x1E},
    {0x107,0x37,0x26,0x9A,0x1F},
    {0x108,0x38,0x2A,0x9B,0x9E},
    {0x109,0x39,0x28,0x29,0x12},
    {0x10A,0x2E,0x3E,0x3E,0x0},
    {0x10B,0x2F,0x3F,0x3F,0x0},
    {0x10C,0x2A,0xC0,0xDF,0x0},
    {0x10D,0x2D,0xDD,0xDC,0x0},
    {0x10E,0x2B,0xDB,0xA6,0x0},
    {0x10F,0xD,0x8D,0x8D,0x0},
    {0x110,0x3D,0x3D,0x3D,0x1F},
    {0x111,0x91,0x91,0x0,0x0},
    {0x112,0x11,0x11,0x0,0x0},
    {0x113,0x1D,0x1D,0x0,0x0},
    {0x114,0x9D,0x9D,0x0,0x0},
    {0x116,0x13,0x93,0x93,0x0},
    {0x117,0x3D,0x3D,0x3D,0x1F},
    {0x11A,0x85,0x85,0x85,0x0},
    {0x11B,0x89,0x89,0x89,0x0},
    {0x11C,0x86,0x86,0x86,0x0},
    {0x11D,0x8A,0x8A,0x8A,0x0},
    {0x11E,0x87,0x87,0x87,0x0},
    {0x11F,0x8B,0x8B,0x8B,0x0},
    {0x120,0x88,0x88,0x88,0x0},
    {0x121,0x8C,0x8C,0x8C,0x0}
};

static const SymbolicKeyMap symbolic_keys[] = {
  {"del",        0x14, 0x94, 0x94, 0x00},
  {"return",     0x0D, 0x8D, 0x8D, 0x00},
  {"right",      0x1D, 0x1D, 0x00, 0x00},
  {"f7",         0x88, 0x8C, 0x88, 0x00},
  {"f1",         0x85, 0x89, 0x85, 0x00},
  {"f3",         0x86, 0x8A, 0x86, 0x00},
  {"f5",         0x87, 0x8B, 0x87, 0x00},
  {"f2",         0x89, 0x89, 0x89, 0x00},
  {"f4",         0x8A, 0x8A, 0x8A, 0x00},
  {"f6",         0x8B, 0x8B, 0x8B, 0x00},
  {"f8",         0x8C, 0x8C, 0x8C, 0x00},
  {"down",       0x11, 0x11, 0x00, 0x00},
  {"three",      0x33, 0x23, 0x96, 0x1C},
  {"w",          0x57, 0xD7, 0xB3, 0x17},
  {"a",          0x41, 0xC1, 0xB0, 0x01},
  {"four",       0x34, 0x24, 0x97, 0x9F},
  {"z",          0x5A, 0xDA, 0xAD, 0x1A},
  {"s",          0x53, 0xD3, 0xAE, 0x13},
  {"e",          0x45, 0xC5, 0xB1, 0x05},
  {"*left shift*",  0x00, 0x00, 0x00, 0x00},
  {"five",       0x35, 0x25, 0x98, 0x9C},
  {"r",          0x52, 0xD2, 0xB2, 0x12},
  {"d",          0x44, 0xC4, 0xAC, 0x04},
  {"six",        0x36, 0x5F, 0x99, 0x1E},
  {"c",          0x43, 0xC3, 0xBC, 0x03},
  {"f",          0x46, 0xC6, 0xBB, 0x06},
  {"t",          0x54, 0xD4, 0xA3, 0x14},
  {"x",          0x58, 0xD8, 0xBD, 0x18},
  {"seven",      0x37, 0x26, 0x9A, 0x1F},
  {"y",          0x59, 0xD9, 0xB7, 0x19},
  {"g",          0x47, 0xC7, 0xA5, 0x07},
  {"eight",      0x38, 0x2A, 0x9B, 0x9E},
  {"b",          0x42, 0xC2, 0xBF, 0x02},
  {"h",          0x48, 0xC8, 0xB4, 0x08},
  {"u",          0x55, 0xD5, 0xB8, 0x15},
  {"v",          0x56, 0xD6, 0xBE, 0x16},
  {"nine",       0x39, 0x28, 0x29, 0x12},
  {"i",          0x49, 0xC9, 0xA2, 0x09},
  {"j",          0x4A, 0xCA, 0xB5, 0x0A},
  {"zero",       0x30, 0x29, 0x30, 0x92},
  {"m",          0x4D, 0xCD, 0xA7, 0x0D},
  {"k",          0x4B, 0xCB, 0xA1, 0x0B},
  {"o",          0x4F, 0xCF, 0xB9, 0x0F},
  {"n",          0x4E, 0xCE, 0xAA, 0x0E},
  {"plus",       0x2D, 0xDD, 0xDC, 0x00},
  {"p",          0x50, 0xD0, 0xAF, 0x10},
  {"l",          0x4C, 0xCC, 0xB6, 0x0C},
  {"minus",      0x3D, 0x2B, 0x00, 0x00},
  {"dot",        0x2E, 0x3E, 0x3E, 0x00},
  {"colon",      0x3B, 0x3A, 0x5D, 0x1D},
  {"at",         0x5B, 0xBA, 0xA4, 0x00},
  {"comma",      0x2C, 0x3C, 0x3C, 0x00},
  {"pound",      0x5C, 0xA9, 0xA8, 0x1C},
  {"star",       0x2A, 0xC0, 0xDF, 0x00},
  {"semicolon",  0x27, 0x22, 0x00, 0x00},
  {"home",       0x13, 0x93, 0x93, 0x00},
  {"*right shift*", 0x00, 0x00, 0x00, 0x00},
  {"equal",      0x3D, 0x3D, 0x3D, 0x1F},
  {"arrowup",    0x5E, 0xDE, 0xDE, 0x00},
  {"slash",      0x2F, 0x3F, 0x3F, 0x00},
  {"one",        0x31, 0x21, 0x81, 0x90},
  {"arrowleft",  0x5F, 0x5F, 0x5F, 0x06},
  {"*control*",  0x00, 0x00, 0x00, 0x00},
  {"two",        0x32, 0x22, 0x95, 0x05},
  {"space",      0x20, 0xA0, 0xA0, 0x00},
  {"*commodore*",0x00, 0x00, 0x00, 0x00},
  {"q",          0x51, 0xD1, 0xAB, 0x11},
  {"stop",       0x03, 0x03, 0x03, 0x03},
  /* Swedish/German specific: direct ÅÖÄ mapping */
  {"se_aa",      0x5B, 0xDB, 0xA4, 0x00},  /* Å: same as 'at' key = PETSCII 0x5B */
  {"se_oe",      0x5C, 0xA9, 0xA8, 0x00},  /* Ö: same as 'pound' key = PETSCII 0x5C */
  {"se_ae",      0x5D, 0xDD, 0x5D, 0x00}   /* Ä: PETSCII 0x5D (]) = screencode 29 */
};

static const SymbolicKeyMap *find_symbolic_key(const char *name) {
  size_t i;
  for (i = 0; i < sizeof(symbolic_keys) / sizeof(symbolic_keys[0]); ++i) {
    if (strcmp(symbolic_keys[i].name, name) == 0) {
      return &symbolic_keys[i];
    }
  }
  return NULL;
}

static void apply_mapping(int value, unsigned char unshifted, unsigned char shifted,
                          unsigned char cbm, unsigned char control) {
  if (value < 0 || value >= SDLK_LAST) {
    return;
  }
  keytable[value][0] = unshifted;
  keytable[value][1] = shifted;
  keytable[value][2] = cbm;
  keytable[value][3] = control;
}

/* Reload keyboard layout from file without reinitializing SDL/UI */
int kbd_reload(char *keyboardcfg) {
  char linebuf[256];
  int value, unshifted, shifted, cbm, control;
  char keyname[64];
  const SymbolicKeyMap *map;
  FILE *in;
  int i;

  memset(keytable, 0, sizeof(keytable));
  for (i = 0; i < (int)(sizeof(DefaultKybdMapping) / sizeof(DefaultKybdMapping[0])); ++i) {
    apply_mapping(DefaultKybdMapping[i][0],
                  (unsigned char)DefaultKybdMapping[i][1],
                  (unsigned char)DefaultKybdMapping[i][2],
                  (unsigned char)DefaultKybdMapping[i][3],
                  (unsigned char)DefaultKybdMapping[i][4]);
  }

  if ((in = fopen(keyboardcfg, "r")) == NULL) {
    printf("Couldn't open %s\n", keyboardcfg);
    return 1;
  }
  while (fgets(linebuf, sizeof(linebuf), in) != NULL) {
    if (strlen(linebuf) >= sizeof(linebuf) - 1) {
      printf("line too long in file: %s\n", keyboardcfg);
      fclose(in);
      return 1;
    }
    if (linebuf[0] == '#' || linebuf[0] == '\n' || linebuf[0] == '\r') {
      continue;
    }
    if (sscanf(linebuf, "%d %d %d %d %d", &value, &unshifted, &shifted, &cbm, &control) == 5) {
      apply_mapping(value, (unsigned char)unshifted, (unsigned char)shifted,
                    (unsigned char)cbm, (unsigned char)control);
    } else if (sscanf(linebuf, "%63s %d", keyname, &value) == 2) {
      map = find_symbolic_key(keyname);
      if (map != NULL) {
        apply_mapping(value, map->unshifted, map->shifted, map->cbm, map->control);
      }
    }
  }
  if (ferror(in)) {
    fclose(in);
    return 1;
  }
  fclose(in);
  return 0;
}


int kbd_init(char *keyboardcfg) {
  char linebuf[256];
  int line = 0, value, unshifted, shifted, cbm, control;
  char keyname[64];
  const SymbolicKeyMap *map;
  FILE *in;
  int i;

  SDL_EnableUNICODE(1);

  kbd_focus = FOCUS_TERM;

  memset(keytable, 0, sizeof(keytable));

  for (i = 0; i < (int)(sizeof(DefaultKybdMapping) / sizeof(DefaultKybdMapping[0])); ++i) {
    apply_mapping(DefaultKybdMapping[i][0],
                  (unsigned char)DefaultKybdMapping[i][1],
                  (unsigned char)DefaultKybdMapping[i][2],
                  (unsigned char)DefaultKybdMapping[i][3],
                  (unsigned char)DefaultKybdMapping[i][4]);
  }

  if ((in = fopen(keyboardcfg, "r")) == NULL) {
    printf("Couldn't open %s\n", keyboardcfg);
  } else {
    while (fgets(linebuf, sizeof(linebuf), in) != NULL) {
      if (strlen(linebuf) >= sizeof(linebuf) - 1) {
        printf("line %d too long in file: %s\n", line, keyboardcfg);
        fclose(in);
        return(1);
      }

      if (linebuf[0] == '#' || linebuf[0] == '\n' || linebuf[0] == '\r') {
        ++line;
        continue;
      }

      if (sscanf(linebuf, "%d %d %d %d %d", &value, &unshifted, &shifted, &cbm, &control) == 5) {
        apply_mapping(value, (unsigned char)unshifted, (unsigned char)shifted,
                      (unsigned char)cbm, (unsigned char)control);
      } else if (sscanf(linebuf, "%63s %d", keyname, &value) == 2) {
        map = find_symbolic_key(keyname);
        if (map != NULL) {
          apply_mapping(value, map->unshifted, map->shifted, map->cbm, map->control);
        } else {
          printf("Unknown key: %s in %s on line %d\n", keyname, keyboardcfg, line);
        }
      } else {
        printf("Syntax error in %s on line %d\n", keyboardcfg, line);
      }

      ++line;
    }

    if (ferror(in)) {
      printf("read error\n");
      fclose(in);
      return(1);
    }

    fclose(in);
  }
  ui_init();
  return(0);
}

void kbd_add_focus(Focus focus, void (*handler)(SDL_keysym *)) {
  focus_focus[focus_count] = focus;
  focus_handler[focus_count] = handler;
  ++focus_count;
}


void kbd_loadseq(char *filename) {// Opens SEQ but doesn't seem to do anything more then open a handle to it.
  if (!seqfile_open) {
    if ((seqfile = fopen(filename, "rb"))) {
      seqfile_open = SDL_TRUE;
    } else {
      printf("opening %s failed\n", filename);
    }
  } else {
    puts("file is already open");
  }
}


void kbd_loadseq_abort(void) {
  if (seqfile_open) {
    fclose(seqfile);
    seqfile_open = SDL_FALSE;
  }
}


unsigned char translatekey(SDL_keysym *keysym, unsigned char *shift, unsigned char *ctrl, unsigned char *cbm) {
  *shift = keysym->mod & (KMOD_SHIFT | KMOD_CAPS) ? 1 : 0;
  *ctrl = keysym->mod & KMOD_CTRL ? 1 : 0;
  *cbm = keysym->mod & KMOD_ALT ? 1 : 0;
  switch (keysym->sym) {

  case SDLK_ESCAPE:
    ui_menu();
    return(0);
    break;

  case SDLK_PAGEUP:
    ui_pageup();
    return(0);
    break;

  case SDLK_PAGEDOWN:
    ui_pagedown();
    return(0);
    break;

  default:
    break;
  }
    if (*shift) {
        return(keytable[keysym->sym][1]);
    } else if (*cbm) {
        return(keytable[keysym->sym])[2];
    } else if (*ctrl) {
        return(keytable[keysym->sym][3]);
    } else {
        return(keytable[keysym->sym][0]);
    }
}

int kbd_getkey() {
    SDL_Event event;
    int c, f;
    unsigned char key = 0;
    
    unsigned char shift, ctrl, cbm;
 
    
    if (SDL_PollEvent(&event)) {
        switch (event.type) {
                
            case SDL_QUIT:
                exit(1);
                break;
                
            case SDL_KEYDOWN:
                
                if (kbd_focus == FOCUS_TERM) {
                    /* Ctrl+ESC or Alt+ESC sends ESC byte to BBS (for ANSI mode) */
                    if (event.key.keysym.sym == SDLK_ESCAPE &&
                        ((event.key.keysym.mod & KMOD_CTRL) || (event.key.keysym.mod & KMOD_ALT))) {
                        return 27;  /* ESC byte */
                    }
                    /* ESC alone opens menu */
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        ui_menu();
                        return 0;
                    } else if (event.key.keysym.mod & KMOD_META) {
                        ui_metakey(&event.key.keysym);
                    } else if ((event.key.keysym.mod & KMOD_CTRL) && event.key.keysym.sym == SDLK_v) {
                        clipboard_paste();
                        return 0;
                    } else if (cfg_termmode == 1) {
                        /* ANSI mode: send ASCII/escape sequences for special keys */
                        switch (event.key.keysym.sym) {
                        case SDLK_UP:    net_send_string((const unsigned char *)"\033[A"); return 0;
                        case SDLK_DOWN:  net_send_string((const unsigned char *)"\033[B"); return 0;
                        case SDLK_RIGHT: net_send_string((const unsigned char *)"\033[C"); return 0;
                        case SDLK_LEFT:  net_send_string((const unsigned char *)"\033[D"); return 0;
                        case SDLK_HOME:  net_send_string((const unsigned char *)"\033[H"); return 0;
                        case SDLK_END:   net_send_string((const unsigned char *)"\033[F"); return 0;
                        case SDLK_BACKSPACE: return 8;
                        case SDLK_DELETE:    return 127;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: return 13;
                        default:
                            /* For printable ASCII, use unicode value directly */
                            if (event.key.keysym.unicode >= 32 && event.key.keysym.unicode < 127) {
                                return (unsigned char)event.key.keysym.unicode;
                            }
                            /* Unicode to CP437 for common extended chars */
                            switch (event.key.keysym.unicode) {
                            case 0x00C4: return 142;  /* Ä */
                            case 0x00C5: return 143;  /* Å */
                            case 0x00D6: return 153;  /* Ö */
                            case 0x00E4: return 132;  /* ä */
                            case 0x00E5: return 134;  /* å */
                            case 0x00F6: return 148;  /* ö */
                            case 0x00FC: return 129;  /* ü */
                            case 0x00DC: return 154;  /* Ü */
                            case 0x00E9: return 130;  /* é */
                            case 0x00C9: return 144;  /* É */
                            case 0x00F1: return 164;  /* ñ */
                            case 0x00D1: return 165;  /* Ñ */
                            case 0x00DF: return 225;  /* ß */
                            case 0x00A3: return 156;  /* £ */
                            }
                            break;
                        }
                        return 0;
                    } else {
                        /* PETSCII mode: Unicode-first input.
                         * ALT = Commodore key, CTRL = C64 Ctrl key.
                         * Regular typing uses SDL unicode → PETSCII conversion.
                         * Modifier combos fall through to keytable lookup. */
                        shift = (event.key.keysym.mod & (KMOD_SHIFT | KMOD_CAPS)) ? 1 : 0;
                        ctrl = (event.key.keysym.mod & KMOD_CTRL) ? 1 : 0;
                        cbm = (event.key.keysym.mod & KMOD_ALT) ? 1 : 0;

                        /* Special keys always use keytable */
                        switch (event.key.keysym.sym) {
                        case SDLK_RETURN: case SDLK_KP_ENTER:
                            return shift ? 0x8D : 0x0D;
                        case SDLK_BACKSPACE:
                            return 0x14;  /* C64 DEL */
                        case SDLK_DELETE:
                            return 0x94;  /* C64 INSERT */
                        case SDLK_HOME:
                            return shift ? 0x93 : 0x13;
                        case SDLK_UP:
                            return 0x91;  /* cursor up */
                        case SDLK_DOWN:
                            return 0x11;  /* cursor down */
                        case SDLK_LEFT:
                            return 0x9D;  /* cursor left */
                        case SDLK_RIGHT:
                            return 0x1D;  /* cursor right */
                        case SDLK_TAB:
                            return 0x09;  /* RUN/STOP */
                        case SDLK_F1:  return shift ? 0x89 : 0x85;
                        case SDLK_F2:  return 0x89;
                        case SDLK_F3:  return shift ? 0x8A : 0x86;
                        case SDLK_F4:  return 0x8A;
                        case SDLK_F5:  return shift ? 0x8B : 0x87;
                        case SDLK_F6:  return 0x8B;
                        case SDLK_F7:  return shift ? 0x8C : 0x88;
                        case SDLK_F8:  return 0x8C;
                        case SDLK_SPACE:
                            return 0x20;
                        case SDLK_PAGEUP:
                            ui_pageup(); return 0;
                        case SDLK_PAGEDOWN:
                            ui_pagedown(); return 0;
                        default:
                            break;
                        }

                        /* ALT (Commodore) + key: use keytable CBM column */
                        if (cbm && keytable[event.key.keysym.sym][2]) {
                            key = keytable[event.key.keysym.sym][2];
                            goto petscii_done;
                        }

                        /* CTRL + key: use keytable Ctrl column (colors etc.) */
                        if (ctrl && keytable[event.key.keysym.sym][3]) {
                            key = keytable[event.key.keysym.sym][3];
                            goto petscii_done;
                        }

                        /* Regular typing: use SDL unicode value → PETSCII */
                        {
                            unsigned int uc = event.key.keysym.unicode;

                            /* Standard ASCII printable range */
                            if (uc >= 32 && uc < 127) {
                                /* PETSCII case swap: lowercase → uppercase PETSCII */
                                if (uc >= 'a' && uc <= 'z') {
                                    key = uc - 32;  /* a(97) → A(65) in PETSCII */
                                } else if (uc >= 'A' && uc <= 'Z') {
                                    key = uc + 128;  /* A(65) → shifted A(193) in PETSCII */
                                } else {
                                    key = (unsigned char)uc;  /* punctuation/numbers = same */
                                }
                                goto petscii_done;
                            }

                            /* Swedish/German characters → PETSCII.
                             * These map to the screencodes where the localized ROM
                             * has the corresponding glyphs. Works regardless of
                             * active charset — the byte is the same, only display differs. */
                            switch (uc) {
                            /* Swedish: å→sc29, ö→sc28, ä→sc27 */
                            case 0x00E5: key = 0x5D; goto petscii_done; /* å */
                            case 0x00C5: key = 0xDD; goto petscii_done; /* Å */
                            case 0x00F6: key = 0x5C; goto petscii_done; /* ö */
                            case 0x00D6: key = 0xDC; goto petscii_done; /* Ö */
                            case 0x00E4: key = 0x5B; goto petscii_done; /* ä */
                            case 0x00C4: key = 0xDB; goto petscii_done; /* Ä */
                            /* German: ü, ß — use same Swedish codes since most
                             * BBSes don't have dedicated German PETSCII support.
                             * ü→same as ä position, ß→same as £ */
                            case 0x00FC: key = 0x5B; goto petscii_done; /* ü → ä position */
                            case 0x00DC: key = 0xDB; goto petscii_done; /* Ü → Ä position */
                            case 0x00DF: key = 0x5C; goto petscii_done; /* ß → ö position */
                            }
                        }

                        /* Fallback: use keytable, then raw sym for ASCII range */
                        if (shift) {
                            key = keytable[event.key.keysym.sym][1];
                        } else {
                            key = keytable[event.key.keysym.sym][0];
                        }
                        /* If keytable returned 0, try using sym directly for ASCII keys */
                        if (key == 0 && event.key.keysym.sym >= 32 && event.key.keysym.sym < 127) {
                            unsigned char sc = (unsigned char)event.key.keysym.sym;
                            if (sc >= 'a' && sc <= 'z') key = sc - 32;
                            else if (sc >= 'A' && sc <= 'Z') key = sc + 128;
                            else key = sc;
                        }

                    petscii_done:
                        if (macro_rec && key != 0) {
                            macrobuf_key[macro_len] = key;
                            macrobuf_shift[macro_len] = shift;
                            macrobuf_ctrl[macro_len] = ctrl;
                            macrobuf_cbm[macro_len] = cbm;
                            if (++macro_len == macro_maxlen) {
                                macro_rec = 0;
                            }
                        }
                        return(key);
                    }
                } else {
                    for (f = 0; f < focus_count; ++f) {
                        if (f >= focus_count) {
                            printf("focus_fixme\n");
                            exit(1);
                        }
                        if (kbd_focus == focus_focus[f]) {
                            focus_handler[f](&event.key.keysym);
                            f = focus_count;
                        }
                    }
                }
                break;
                
        }
    }
    
    if (seqfile_open) {
        if ((c = fgetc(seqfile)) == EOF) {
            fclose(seqfile);
            seqfile_open = SDL_FALSE;
            return(0);
        } else {
            return(c + 256);
        }
    }
    
    if (macro_play) {
        key = macrobuf_key[macro_ctr];
        shift = macrobuf_shift[macro_ctr];
        ctrl = macrobuf_ctrl[macro_ctr];
        cbm = macrobuf_cbm[macro_ctr];
        if (++macro_ctr == macro_len) {
            macro_play = 0;
        }
        return(key);
    }
    return(key);
}

