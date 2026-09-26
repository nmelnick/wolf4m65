#include <mega65.h>
#include <stdint.h>
#include <string.h>

#include "m65_video.h"

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------

// Enhanced-DMA job: option bytes followed by an F018B list.
struct dma_job {
    uint8_t opt_f018b;        // 0x0b
    uint8_t opt_src_mb;       // 0x80
    uint8_t src_mb;
    uint8_t opt_dst_mb;       // 0x81
    uint8_t dst_mb;
    uint8_t opt_end;          // 0x00
    struct DMAList_F018B list;
};

static struct dma_job job;

static void dma_run(uint8_t cmd, uint32_t dst, uint32_t src, uint16_t count)
{
    job.opt_f018b  = ENABLE_F018B_OPT;
    job.opt_src_mb = SRC_ADDR_BITS_OPT;
    job.src_mb     = (uint8_t)(src >> 20);
    job.opt_dst_mb = DST_ADDR_BITS_OPT;
    job.dst_mb     = (uint8_t)(dst >> 20);
    job.opt_end    = 0;

    job.list.command     = cmd;
    job.list.count       = count;
    job.list.source_addr = (uint16_t)src;
    job.list.source_bank = (uint8_t)(src >> 16) & 0x0f;
    job.list.dest_addr   = (uint16_t)dst;
    job.list.dest_bank   = (uint8_t)(dst >> 16) & 0x0f;
    job.list.command_msb = 0;
    job.list.modulo      = 0;

    DMA.enable_f018b = 1;
    DMA.addr_mb   = 0;
    DMA.addr_bank = 0;
    DMA.addr_msb  = (uint8_t)((uint16_t)&job >> 8);
    DMA.trigger_enhanced = (uint8_t)((uint16_t)&job & 0xff);
}

void m65_dma_fill(uint32_t dst, uint8_t value, uint16_t count)
{
    dma_run(DMA_FILL_CMD, dst, value, count);
}

void m65_dma_copy(uint32_t dst, uint32_t src, uint16_t count)
{
    dma_run(DMA_COPY_CMD, dst, src, count);
}

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

    // Colour RAM (attributes) must be clear: 2 bytes per cell.
    m65_dma_fill(0xff80000UL, 0, M65_CELLS_X * M65_CELLS_Y * 2);

    // Full-colour character data is addressed as (cell number * 64) from
    // address 0 (CHARPTR only applies to the normal charset), so cell n of
    // the framebuffer is numbered FB_BASE/64 + n.
    {
        static uint8_t row[M65_CELLS_X * 2];
        unsigned i, y;
        for (y = 0; y < M65_CELLS_Y; y++) {
            for (i = 0; i < M65_CELLS_X; i++) {
                unsigned n = (unsigned)(M65_FB_BASE >> 6) + y * M65_CELLS_X + i;
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

void m65_put_scanline(unsigned x, unsigned y, const uint8_t *src, unsigned width)
{
    unsigned i;
    for (i = 0; i < width; i += 8)
        m65_dma_copy(m65_fb_addr(x + i, y), (uint32_t)(uintptr_t)(src + i), 8);
}
