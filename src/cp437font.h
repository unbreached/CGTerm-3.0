#ifndef CP437FONT_H
#define CP437FONT_H

#include <SDL.h>

SDL_Surface *cp437_create_raw_font(void);
const unsigned char *cp437_glyph(int c);

#endif
