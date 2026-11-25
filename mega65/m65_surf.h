// Pixel access on far SDL surfaces (see SDL.h), for both layouts.
//
// Rows are the unit of bulk work: a row is read or written through a near
// buffer, with one DMA job per contiguous run (a whole row for a linear
// surface, up to 8 pixels per character cell for a tiled one).
// Coordinates must already be clipped to the surface.

#ifndef M65_SURF_H
#define M65_SURF_H

#include <stdint.h>
#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

// Every surface's pixel format: 8 bits a pixel (m65_sdl.c).
extern SDL_PixelFormat m65_format8;

uint32_t surf_addr(const SDL_Surface *s, unsigned x, unsigned y);

void surf_write_row(const SDL_Surface *s, unsigned x, unsigned y,
                    const uint8_t *src, unsigned n);
void surf_read_row(const SDL_Surface *s, unsigned x, unsigned y,
                   uint8_t *dst, unsigned n);
void surf_fill_row(const SDL_Surface *s, unsigned x, unsigned y,
                   unsigned n, uint8_t color);

void    surf_plot(const SDL_Surface *s, unsigned x, unsigned y, uint8_t color);
uint8_t surf_get(const SDL_Surface *s, unsigned x, unsigned y);

// Rectangle fill and copy (clipped by the caller). Copies go row by row.
void surf_fill_rect(const SDL_Surface *s, unsigned x, unsigned y,
                    unsigned w, unsigned h, uint8_t color);
void surf_copy_rect(const SDL_Surface *src, unsigned sx, unsigned sy,
                    const SDL_Surface *dst, unsigned dx, unsigned dy,
                    unsigned w, unsigned h);

#ifdef __cplusplus
}
#endif

#endif
