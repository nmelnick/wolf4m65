// VIC-IV set-up and palette (overlay-able; the DMA helpers in m65_video.c
// stay resident).
#include <mega65.h>
#include <stdint.h>
#include <string.h>

#include "m65_video.h"

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

// Register: the palette stores each channel with its nybbles swapped.
static uint8_t swapnyb(uint8_t v)
{
    return (uint8_t)((v << 4) | (v >> 4));
}

void m65_set_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    PALETTE.red[index]   = swapnyb(r);
    PALETTE.green[index] = swapnyb(g);
    PALETTE.blue[index]  = swapnyb(b);
}

void m65_video_init(void)
{
    // Unlock VIC-IV.
    VICIV.key = 0x47;
    VICIV.key = 0x53;

    // Freeze the VIC-II "hot registers" so writes below don't recompute
    // the display parameters half way through.
    VICIV.sdbdrwd_msb &= (uint8_t)~VIC4_HOTREG_MASK;

    // The ROM boots in 80-column mode; we want 40 cells of 8 pixels.
    VICIV.ctrlb &= (uint8_t)~VIC3_H640_MASK;

    // 40 MHz, 16-bit screen cells, full-colour characters.
    VICIV.ctrlc = VIC4_VFAST_MASK | VIC4_CHR16_MASK
                | VIC4_FCLRLO_MASK | VIC4_FCLRHI_MASK;

    // Use the RAM palette for every layer: bank 0 selected for all.
    VICIV.ctrla |= VIC3_PAL_MASK;
    VICIV.palsel = 0;

    // Screen geometry: 40 cells per row, 2 bytes per cell.
    VICIV.linestep = M65_CELLS_X * 2;
    VICIV.chrcount = M65_CELLS_X;
    VICIV.scrnptr  = M65_SCREENRAM;

    // Colour RAM (attributes) must be clear: 2 bytes per cell, from its
    // start (COLPTR: the boot mode may have left it elsewhere, and the rest
    // of colour RAM is the texture cache, id_pm.cpp).
    VICIV.colptr = 0;
    m65_dma_fill(0xff80000UL, 0, M65_CELLS_X * M65_CELLS_Y * 2);

    // Full-colour character data is addressed as (cell number * 64) from
    // address 0 (CHARPTR only applies to the normal charset). Cells are laid
    // out column-major (see m65_video.h): the cell at column i, row y is
    // FB_BASE/64 + i * CELLS_Y + y.
    {
        static uint8_t row[M65_CELLS_X * 2];
        unsigned i, y;
        for (y = 0; y < M65_CELLS_Y; y++) {
            for (i = 0; i < M65_CELLS_X; i++) {
                unsigned n = (unsigned)(M65_FB_BASE >> 6) + i * M65_CELLS_Y + y;
                row[i * 2]     = (uint8_t)n;
                row[i * 2 + 1] = (uint8_t)(n >> 8);
            }
            m65_dma_copy(M65_SCREENRAM + (uint32_t)y * M65_CELLS_X * 2,
                         (uint32_t)(uintptr_t)row, sizeof row);
        }
    }

    VICIV.bordercol = 0;
    VICIV.screencol = 0;

    m65_dma_fill(M65_FB_BASE, 0, 32000);
    m65_dma_fill(M65_FB_BASE + 32000UL, 0, 32000);
}
