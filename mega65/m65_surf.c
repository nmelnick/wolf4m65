#include <stdint.h>

#include "m65_far.h"
#include "m65_surf.h"
#include "m65_video.h"

uint32_t surf_addr(const SDL_Surface *s, unsigned x, unsigned y)
{
    if (s->tiled)                   // column-major cells: see m65_video.h
        return s->farpixels + (uint32_t)(x >> 3) * M65_CELLCOL_SIZE
             + ((uint16_t)y << 3) + (x & 7);
    return s->farpixels + (uint32_t)y * s->pitch + x;
}

// Length of the contiguous run starting at x (bounded by n).
static unsigned run_len(const SDL_Surface *s, unsigned x, unsigned n)
{
    unsigned r;
    if (!s->tiled)
        return n;
    r = 8 - (x & 7);
    return r < n ? r : n;
}

void surf_write_row(const SDL_Surface *s, unsigned x, unsigned y,
                    const uint8_t *src, unsigned n)
{
    while (n) {
        unsigned r = run_len(s, x, n);
        m65_dma_copy(surf_addr(s, x, y), (uint32_t)(uintptr_t)src, r);
        x += r; src += r; n -= r;
    }
}

void surf_read_row(const SDL_Surface *s, unsigned x, unsigned y,
                   uint8_t *dst, unsigned n)
{
    while (n) {
        unsigned r = run_len(s, x, n);
        m65_dma_copy((uint32_t)(uintptr_t)dst, surf_addr(s, x, y), r);
        x += r; dst += r; n -= r;
    }
}

void surf_fill_row(const SDL_Surface *s, unsigned x, unsigned y,
                   unsigned n, uint8_t color)
{
    while (n) {
        unsigned r = run_len(s, x, n);
        m65_dma_fill(surf_addr(s, x, y), color, r);
        x += r; n -= r;
    }
}

void surf_plot(const SDL_Surface *s, unsigned x, unsigned y, uint8_t color)
{
    far_poke(FAR(surf_addr(s, x, y)), color);
}

uint8_t surf_get(const SDL_Surface *s, unsigned x, unsigned y)
{
    return far_peek(FAR(surf_addr(s, x, y)));
}

void surf_fill_rect(const SDL_Surface *s, unsigned x, unsigned y,
                    unsigned w, unsigned h, uint8_t color)
{
    // A whole tiled screen is one contiguous block.
    if (s->tiled && x == 0 && y == 0 && w == M65_SCREEN_W && h == M65_SCREEN_H) {
        m65_dma_fill(s->farpixels, color, 32000);
        m65_dma_fill(s->farpixels + 32000, color, 32000);
        return;
    }
    if (s->tiled) {
        // Column-major cells: rows y..y+h-1 of one 8-pixel strip are one
        // contiguous block, and a single pixel column is every 8th byte.
        while (w) {
            unsigned r = 8 - (x & 7);
            if (r > w) r = w;
            if (r == 8)
                m65_dma_fill(surf_addr(s, x, y), color, (uint16_t)(h << 3));
            else {
                unsigned i;
                for (i = 0; i < r; i++)
                    m65_dma_fill_skip(surf_addr(s, x + i, y), color, h, M65_COLUMN_STEP);
            }
            x += r; w -= r;
        }
        return;
    }
    while (h--)
        surf_fill_row(s, x, y++, w, color);
}

void surf_copy_rect(const SDL_Surface *src, unsigned sx, unsigned sy,
                    const SDL_Surface *dst, unsigned dx, unsigned dy,
                    unsigned w, unsigned h)
{
    static uint8_t row[M65_SCREEN_W];

    // Whole screen between two surfaces of the same layout: one block.
    if (src->tiled == dst->tiled && sx == 0 && sy == 0 && dx == 0 && dy == 0 &&
        w == (unsigned)src->w && h == (unsigned)src->h &&
        src->w == dst->w && src->h == dst->h &&
        (src->tiled || src->pitch == dst->pitch)) {
        uint32_t size = src->tiled ? 64000UL : (uint32_t)src->pitch * h;
        uint32_t done = 0;
        while (done < size) {
            uint16_t n = size - done > 32000 ? 32000 : (uint16_t)(size - done);
            m65_dma_copy(dst->farpixels + done, src->farpixels + done, n);
            done += n;
        }
        return;
    }
    while (w > sizeof row) {        // (wider than any row buffer we need)
        surf_copy_rect(src, sx, sy, dst, dx, dy, sizeof row, h);
        sx += sizeof row; dx += sizeof row; w -= sizeof row;
    }
    while (h--) {
        surf_read_row(src, sx, sy++, row, w);
        surf_write_row(dst, dx, dy++, row, w);
    }
}
