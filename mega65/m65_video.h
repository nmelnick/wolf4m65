// MEGA65 video layer: VIC-IV 320x200, 256 colours.
//
// The display is VIC-IV full-colour character mode with 16-bit screen
// entries, so the framebuffer is *tiled*: 40x25 cells of 8x8 pixels, each
// cell 64 contiguous bytes (row-major inside the cell).
//
//   addr(x, y) = M65_FB_BASE + ((y >> 3) * 40 + (x >> 3)) * 64
//                            + (y & 7) * 8 + (x & 7)

#ifndef M65_VIDEO_H
#define M65_VIDEO_H

#include <stdint.h>

#define M65_SCREEN_W   320
#define M65_SCREEN_H   200
#define M65_CELLS_X    (M65_SCREEN_W / 8)
#define M65_CELLS_Y    (M65_SCREEN_H / 8)

#define M65_FB_BASE      0x40000UL   // displayed pixel data (64000 bytes)
#define M65_FB2_BASE     0x50000UL   // second framebuffer (the game's draw target)
#define M65_SCREENRAM    0x12000UL   // 40x25 16-bit cell numbers

static inline uint32_t m65_fb_addr(unsigned x, unsigned y)
{
    return M65_FB_BASE
         + ((uint32_t)((y >> 3) * M65_CELLS_X + (x >> 3)) << 6)
         + ((y & 7) << 3) + (x & 7);
}

void m65_video_init(void);

// 8-bit-per-channel palette entry.
void m65_set_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// Fill and copy anywhere in the 28-bit address space, using DMA.
void m65_dma_fill(uint32_t dst, uint8_t value, uint16_t count);
void m65_dma_copy(uint32_t dst, uint32_t src, uint16_t count);

// Copy one linear scanline (width <= 320, x a multiple of 8) from normal
// memory into the tiled framebuffer.
void m65_put_scanline(unsigned x, unsigned y, const uint8_t *src, unsigned width);

#endif
