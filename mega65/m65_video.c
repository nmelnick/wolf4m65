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
    uint8_t opt_dst_skip;     // 0x85: destination step, whole bytes
    uint8_t dst_skip;
    uint8_t opt_end;          // 0x00
    struct DMAList_F018B list;
};

static struct dma_job job;

// A count of 0 does nothing (to the DMA controller it means 64KB). Callers
// rely on this: e.g. m65_takeover passes linker-symbol sizes, which the
// compiler assumes to be non-zero, so a guard there is optimised away.
static void dma_run(uint8_t cmd, uint32_t dst, uint32_t src, uint16_t count,
                    uint8_t dstskip)
{
    if (!count)
        return;
    job.opt_f018b  = ENABLE_F018B_OPT;
    job.opt_src_mb = SRC_ADDR_BITS_OPT;
    job.src_mb     = (uint8_t)(src >> 20);
    job.opt_dst_mb = DST_ADDR_BITS_OPT;
    job.dst_mb     = (uint8_t)(dst >> 20);
    job.opt_dst_skip = DST_SKIP_RATE_OPT;
    job.dst_skip   = dstskip;
    job.opt_end    = 0;

    job.list.command     = cmd;
    job.list.count       = count;
    job.list.source_addr = (uint16_t)src;
    job.list.source_bank = (uint8_t)(src >> 16) & 0x0f;
    job.list.dest_addr   = (uint16_t)dst;
    job.list.dest_bank   = (uint8_t)(dst >> 16) & 0x0f;
    job.list.command_msb = 0;
    job.list.modulo      = 0;

    // The DMA engine reads `job` and writes memory behind the compiler's back:
    // barriers stop it from dropping or reordering the list stores (which it
    // did once LTO inlined two back-to-back copies), and from assuming the
    // destination still holds what it held before.
    __asm__ volatile("" ::: "memory");
    DMA.enable_f018b = 1;
    DMA.addr_mb   = 0;
    DMA.addr_bank = 0;
    DMA.addr_msb  = (uint8_t)((uint16_t)&job >> 8);
    DMA.trigger_enhanced = (uint8_t)((uint16_t)&job & 0xff);
    __asm__ volatile("" ::: "memory");
}

void m65_dma_fill(uint32_t dst, uint8_t value, uint16_t count)
{
    dma_run(DMA_FILL_CMD, dst, value, count, 1);
}

void m65_dma_copy(uint32_t dst, uint32_t src, uint16_t count)
{
    dma_run(DMA_COPY_CMD, dst, src, count, 1);
}

void m65_dma_copy_skip(uint32_t dst, uint32_t src, uint16_t count, uint8_t dstskip)
{
    dma_run(DMA_COPY_CMD, dst, src, count, dstskip);
}

void m65_dma_fill_skip(uint32_t dst, uint8_t value, uint16_t count, uint8_t dstskip)
{
    dma_run(DMA_FILL_CMD, dst, value, count, dstskip);
}

