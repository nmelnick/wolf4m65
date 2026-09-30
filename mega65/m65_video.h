// MEGA65 video layer: VIC-IV 320x200, 256 colours.
//
// The display is VIC-IV full-colour character mode with 16-bit screen
// entries, so the framebuffer is *tiled*: 40x25 cells of 8x8 pixels, each
// cell 64 contiguous bytes (row-major inside the cell). The cells are
// numbered column-major (each cell directly below the previous one in
// memory), which makes every vertical pixel column a uniform stride of 8
// bytes over the full height - Wolf3D draws in columns:
//
//   addr(x, y) = M65_FB_BASE + (x >> 3) * 1600 + y * 8 + (x & 7)
//
// A horizontal run is contiguous within a cell (8 pixels).

#ifndef M65_VIDEO_H
#define M65_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M65_SCREEN_W   320
#define M65_SCREEN_H   200
#define M65_CELLS_X    (M65_SCREEN_W / 8)
#define M65_CELLS_Y    (M65_SCREEN_H / 8)

#define M65_FB_BASE      0x40000UL   // displayed pixel data (64000 bytes)
#define M65_FB2_BASE     0x50000UL   // second framebuffer (the game's draw target)
#define M65_SCREENRAM    0x12000UL   // 40x25 16-bit cell numbers

#define M65_COLUMN_STEP  8                         // next pixel down
#define M65_CELLCOL_SIZE (M65_CELLS_Y * 64)        // one 8-pixel-wide strip: 1600

void m65_video_init(void);

// 8-bit-per-channel palette entry.
void m65_set_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// Fill and copy anywhere in the 28-bit address space, using DMA.
void m65_dma_fill(uint32_t dst, uint8_t value, uint16_t count);
void m65_dma_copy(uint32_t dst, uint32_t src, uint16_t count);

// The fill, writing every `dstskip`-th destination byte (a column of the
// framebuffer is every 8th byte; see m65_video.h's layout).
void m65_dma_fill_skip(uint32_t dst, uint8_t value, uint16_t count, uint8_t dstskip);
// Scaled copy: the source steps by srcstep (8.8 fixed point) per byte copied.
// The sprite scaler's DMA jobs: m65_dma_col_setup sets the buffer's base and
// the source step, then each m65_dma_segs(xoff, n) copies posts 0..n-1
// (m65_segsrc[k], m65_segcount[k] rows, to row m65_segtop[k]) to the
// column at offset xoff in the buffer, every 8th byte.
#define M65_MAXSEGS 16
extern uint8_t m65_segtop[M65_MAXSEGS], m65_segcount[M65_MAXSEGS];
extern uint32_t m65_segsrc[M65_MAXSEGS];
void m65_dma_col_setup(uint32_t base, uint16_t srcstep);
void m65_dma_segs(uint16_t xoff, uint8_t n);

void m65_dma_scale(uint32_t dst, uint32_t src, uint16_t count, uint16_t srcstep,
                   uint8_t dstskip);

#ifdef __cplusplus
}
#endif

#endif
