#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "font.h"


static SDL_Surface *font_draw_surface;
static Font *font_current;


int font_init(SDL_Surface *surface) {
  font_draw_surface = surface;
  font_current = NULL;
  return(0);
}


Font *font_load_font(const char *filename, int charw, int charh, int fontw, int fonth) {
  SDL_Surface *tempsurface, *fontsurface;
  Font *font;
  int numchars;
  int x, y, c;
  SDL_Rect src, dest;

  numchars = fontw * fonth;

  if ((fontsurface = SDL_LoadBMP(filename)) == NULL) {
    printf("Unable to load %s: %s\n", filename, SDL_GetError());
    return(NULL);
  }
  if (fontsurface->format->BytesPerPixel != 1) {
    printf("%s can't be used as a font\n", filename);
    SDL_FreeSurface(fontsurface);
    return(NULL);
  }

  if (charw > fontsurface->w / fontw || charh > fontsurface->h / fonth) {
    printf("Font size mismatch\n");
    SDL_FreeSurface(fontsurface);
    return(NULL);
  }
  dest.w = src.w = charw;
  dest.h = src.h = charh;

  if ((tempsurface = SDL_CreateRGBSurface(0, numchars * charw, charh, 8, 0, 0, 0, 0)) == NULL) {
    printf("Surface allocation failed: %s\n", SDL_GetError());
    SDL_FreeSurface(fontsurface);
    return(NULL);
  }
  SDL_SetPalette(tempsurface, SDL_LOGPAL|SDL_PHYSPAL, fontsurface->format->palette->colors, 0, fontsurface->format->palette->ncolors);
  SDL_SetColorKey(tempsurface, SDL_SRCCOLORKEY, 0);

  c = 0;
  for (y = 0; y < fonth; ++y) {
    for (x = 0; x < fontw; ++x) {
      src.x = x * charw;
      src.y = y * charh;
      dest.x = c * charw;
      dest.y = 0;
      SDL_BlitSurface(fontsurface, &src, tempsurface, &dest);
      ++c;
    }
  }
  SDL_FreeSurface(fontsurface);

  if ((font = malloc(sizeof *font)) == NULL) {
    return(NULL);
  }
  /*
  font->surface = SDL_DisplayFormat(tempsurface);
  SDL_FreeSurface(tempsurface);
  if (font->surface == NULL) {
    free(font);
    return(NULL);
  }
  */
  font->surface = tempsurface;
  font->width = charw;
  font->height = charh;
  font->numchars = numchars;
  return(font);
}


void font_free(Font *font) {
  SDL_FreeSurface(font->surface);
  free(font);
}


Font *font_set_font(Font *font) {
  Font *oldfont;

  oldfont = font_current;
  font_current = font;
  return(oldfont);
}


SDL_Surface *font_set_draw_surface(SDL_Surface *surface) {
  SDL_Surface *oldsurface;

  oldsurface = font_draw_surface;
  font_draw_surface = surface;
  return(oldsurface);
}


void font_draw_string(int x, int y, const char *text) {
  SDL_Rect src, dest;

  src.w = dest.w = font_current->width;
  src.h = dest.h = font_current->height;
  dest.x = x;
  dest.y = y;
  src.y = 0;
  while (*text) {
    src.x = (*text) * src.w;
    SDL_BlitSurface(font_current->surface, &src, font_draw_surface, &dest);
    ++text;
    dest.x += src.w;
  }
}


void font_draw_string_color(int x, int y, const char *text, int r, int g, int b) {
  /* Direct pixel rendering — bypasses SDL's blit map cache entirely.
   * Reads 8-bit source pixels, writes colored 32-bit pixels to dest. */
  SDL_Surface *src = font_current->surface;
  SDL_Surface *dst = font_draw_surface;
  int cw = font_current->width;
  int ch = font_current->height;
  Uint32 col = SDL_MapRGB(dst->format, r, g, b);
  int dx = x;

  SDL_LockSurface(src);
  SDL_LockSurface(dst);

  while (*text) {
    int sx = (unsigned char)(*text) * cw;
    int px, py;

    for (py = 0; py < ch; py++) {
      int dsty = y + py;
      if (dsty < 0 || dsty >= dst->h) continue;

      for (px = 0; px < cw; px++) {
        int dstx = dx + px;
        int srcx = sx + px;
        Uint8 pixel;

        if (dstx < 0 || dstx >= dst->w) continue;
        if (srcx < 0 || srcx >= src->w) continue;

        /* Read 8-bit palette index from font surface */
        pixel = ((Uint8 *)src->pixels)[py * src->pitch + srcx];

        /* Skip color-key (index 0 = transparent) */
        if (pixel != 0) {
          /* Write colored pixel to 32-bit destination */
          memcpy((Uint8 *)dst->pixels + dsty * dst->pitch +
                 dstx * dst->format->BytesPerPixel,
                 &col, dst->format->BytesPerPixel);
        }
      }
    }

    ++text;
    dx += cw;
  }

  SDL_UnlockSurface(dst);
  SDL_UnlockSurface(src);
}


void font_draw_string_color_scaled(int x, int y, const char *text,
                                   int r, int g, int b, int scale) {
  SDL_Surface *src = font_current->surface;
  SDL_Surface *dst = font_draw_surface;
  int cw = font_current->width;
  int ch = font_current->height;
  Uint32 col = SDL_MapRGB(dst->format, r, g, b);
  int bpp = dst->format->BytesPerPixel;
  int dx = x;

  if (scale < 1) scale = 1;

  SDL_LockSurface(src);
  SDL_LockSurface(dst);

  while (*text) {
    int sx = (unsigned char)(*text) * cw;
    int px, py;

    for (py = 0; py < ch; py++) {
      for (px = 0; px < cw; px++) {
        int srcx = sx + px;
        Uint8 pixel;

        if (srcx < 0 || srcx >= src->w) continue;
        pixel = ((Uint8 *)src->pixels)[py * src->pitch + srcx];

        if (pixel != 0) {
          int sy, sxx;
          for (sy = 0; sy < scale; sy++) {
            int dsty = y + py * scale + sy;
            if (dsty < 0 || dsty >= dst->h) continue;
            for (sxx = 0; sxx < scale; sxx++) {
              int dstx = dx + px * scale + sxx;
              if (dstx < 0 || dstx >= dst->w) continue;
              memcpy((Uint8 *)dst->pixels + dsty * dst->pitch + dstx * bpp,
                     &col, bpp);
            }
          }
        }
      }
    }

    ++text;
    dx += cw * scale;
  }

  SDL_UnlockSurface(dst);
  SDL_UnlockSurface(src);
}


/* Float-scale single character rendering for smooth zoom effects */
void font_draw_char_color_fscale(int ch_code, int x, int y,
                                  int r, int g, int b, float fscale) {
  SDL_Surface *src = font_current->surface;
  SDL_Surface *dst = font_draw_surface;
  int cw = font_current->width;
  int chh = font_current->height;
  Uint32 col = SDL_MapRGB(dst->format, r, g, b);
  int bpp = dst->format->BytesPerPixel;
  int sx_base = (ch_code & 0xFF) * cw;
  int scaled_w = (int)(cw * fscale);
  int scaled_h = (int)(chh * fscale);
  int px, py;

  if (fscale < 0.1f) return;

  SDL_LockSurface(src);
  SDL_LockSurface(dst);

  for (py = 0; py < scaled_h; py++) {
    int dsty = y + py;
    int src_y = (int)((float)py / fscale);
    if (dsty < 0 || dsty >= dst->h) continue;
    if (src_y >= chh) src_y = chh - 1;

    for (px = 0; px < scaled_w; px++) {
      int dstx = x + px;
      int src_x = (int)((float)px / fscale);
      Uint8 pixel;

      if (dstx < 0 || dstx >= dst->w) continue;
      if (src_x >= cw) src_x = cw - 1;

      pixel = ((Uint8 *)src->pixels)[src_y * src->pitch + sx_base + src_x];
      if (pixel != 0) {
        memcpy((Uint8 *)dst->pixels + dsty * dst->pitch + dstx * bpp,
               &col, bpp);
      }
    }
  }

  SDL_UnlockSurface(dst);
  SDL_UnlockSurface(src);
}
