// Overlay prototype: resident side.
//
// Verified in Xemu (not yet on real hardware):
//   * LLD OVERLAY works: overlays are linked at $8000 and stored in the PRG
//     at $C000+; startup copies them to attic RAM by DMA.
//   * MAP pages attic RAM into $8000-$BFFF; code runs correctly from there.
//   * Attic MB $80 offset 0 is NOT free (something else owns it); use MB $81+.
//   * A MAP instruction replaces BOTH regions' mappings, and each region has
//     a single offset+MB shared by all its mapped 8K blocks. On the MEGA65 the
//     ROM (KERNAL at $E000) is itself provided by the upper region's mapping,
//     so this MAP unmaps the KERNAL: printf/stdio hang afterwards.
//   * Z must be 0 for compiled code, so restore it (ldz #0) after MAP.
//   * A DMA read-back of attic offset 0 (slot 0) did not land in the target
//     buffer although the data was demonstrably there (code ran). Unexplained.
#include <mega65.h>
#include <stdint.h>
#include <stdio.h>

extern char __ovl0_lma[], __ovl0_size[], __ovl1_lma[], __ovl1_size[];

#define ATTIC_MB      0x81
#define OVL_SLOT_SIZE 0x4000UL     // one overlay slot in attic
#define OVL_ATTIC(k)  (0x8100000UL + (k) * OVL_SLOT_SIZE)

// --- DMA copy (enhanced, 28-bit both sides) ---------------------------------
struct dma_job {
    uint8_t opt_f018b, opt_src_mb, src_mb, opt_dst_mb, dst_mb, opt_end;
    struct DMAList_F018B list;
};
static struct dma_job job;

static void dma_copy(uint32_t dst, uint32_t src, uint16_t count)
{
    job.opt_f018b = ENABLE_F018B_OPT;
    job.opt_src_mb = SRC_ADDR_BITS_OPT;  job.src_mb = (uint8_t)(src >> 20);
    job.opt_dst_mb = DST_ADDR_BITS_OPT;  job.dst_mb = (uint8_t)(dst >> 20);
    job.opt_end = 0;
    job.list.command = DMA_COPY_CMD;
    job.list.count = count;
    job.list.source_addr = (uint16_t)src;
    job.list.source_bank = (uint8_t)(src >> 16) & 0x0f;
    job.list.dest_addr = (uint16_t)dst;
    job.list.dest_bank = (uint8_t)(dst >> 16) & 0x0f;
    job.list.command_msb = 0;
    job.list.modulo = 0;
    DMA.enable_f018b = 1;
    DMA.addr_mb = 0;
    DMA.addr_bank = 0;
    DMA.addr_msb = (uint8_t)((uint16_t)&job >> 8);
    DMA.trigger_enhanced = (uint8_t)((uint16_t)&job & 0xff);
}

// --- MAP -------------------------------------------------------------------
// Upper region ($8000-$FFFF): blocks $8000 and $A000 mapped to attic slot k.
static void ovl_map(uint8_t k)
{
    uint16_t off = (uint16_t)((k * 0x4000UL - 0x8000UL) >> 8) & 0x0fff;
    uint8_t ylo = (uint8_t)off;
    uint8_t zhi = (uint8_t)(0x30 | (off >> 8));   // mask: $8000 + $A000 blocks

    __asm__ volatile(
        "lda #0\n"        // lower region: nothing mapped
        "ldx #0\n"
        "ldy %0\n"
        "ldz %1\n"
        "map\n"
        "eom\n"
        "ldz #0\n"
        :: "r"(ylo), "r"(zhi) : "a", "x", "y", "c");
}

// Select attic megabyte for the upper region (once).
static void ovl_setmb(void)
{
    __asm__ volatile(
        "lda #0\n"
        "ldx #$0f\n"
        "ldy #%0\n"
        "ldz #$0f\n"
        "map\n"
        "eom\n"
        "ldz #0\n"
        :: "i"(ATTIC_MB) : "a", "x", "y", "c");
}

// The overlay entry points (defined in ovl_a.c / ovl_b.c).
int ov_add(int a, int b);
int ov_mul(int a, int b);

#define MARK(n, v) (*(volatile uint8_t *)(0x0400 + (n)) = (v))
#define RES16(n, v) (*(volatile uint16_t *)(0x0410 + (n) * 2) = (uint16_t)(v))

int main(void)
{
    int r1, r2;

    __asm__ volatile("sei");
    MARK(0, 1);

    dma_copy(OVL_ATTIC(0), (uint32_t)(uintptr_t)__ovl0_lma, (uint16_t)(uintptr_t)__ovl0_size);
    dma_copy(OVL_ATTIC(1), (uint32_t)(uintptr_t)__ovl1_lma, (uint16_t)(uintptr_t)__ovl1_size);
    MARK(1, 2);
    dma_copy(0x0500, OVL_ATTIC(0), 32);   // read back for inspection
    dma_copy(0x0540, OVL_ATTIC(1), 32);

    {
        volatile uint8_t *p;
        uint8_t i;
        p = (volatile uint8_t *)0xC000; for (i = 0; i < 16; i++) *(volatile uint8_t *)(0x0600 + i) = p[i];
        p = (volatile uint8_t *)0xE000; for (i = 0; i < 16; i++) *(volatile uint8_t *)(0x0610 + i) = p[i];
    }
    ovl_setmb();
    MARK(2, 3);

    ovl_map(0);
    MARK(3, 4);
    {
        volatile uint8_t *p;
        uint8_t i;
        p = (volatile uint8_t *)0xC000; for (i = 0; i < 16; i++) *(volatile uint8_t *)(0x0620 + i) = p[i];
        p = (volatile uint8_t *)0xE000; for (i = 0; i < 16; i++) *(volatile uint8_t *)(0x0630 + i) = p[i];
        p = (volatile uint8_t *)0x8000; for (i = 0; i < 16; i++) *(volatile uint8_t *)(0x0640 + i) = p[i];
    }
    r1 = ov_add(20, 22);
    MARK(4, 5);
    ovl_map(1);
    r2 = ov_mul(6, 7);
    MARK(5, 6);
    ovl_map(0);
    RES16(0, r1);
    RES16(1, r2);
    RES16(2, ov_add(1, 2));
    MARK(6, 7);

    for (;;) {}
}
