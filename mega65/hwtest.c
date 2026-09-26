// Hardware timing test: what costs what on this machine (make hwtest.prg).
//
// Times, with the CIA 2 millisecond timer the game uses (SDL_GetTicks):
//   - the timer itself, against 50 video frames (1000 ms on PAL);
//   - a busy loop run from chip RAM, and the same loop run from attic RAM
//     through the overlay window at $4000 (how the game runs its code);
//   - reading 64KB from chip RAM and from attic RAM (32-bit pointer);
//   - DMA copies of 64000 bytes, chip to chip and attic to chip.
// The results are shown on the screen (and sent to the debug serial port).
// Run it straight after power-on or reset: it takes the machine over.

#include <stdint.h>

#include "m65_debug.h"
#include "m65_video.h"

#define CIA2_TALO (*(volatile uint8_t *)0xDD04)
#define CIA2_TAHI (*(volatile uint8_t *)0xDD05)
#define CIA2_TBLO (*(volatile uint8_t *)0xDD06)
#define CIA2_TBHI (*(volatile uint8_t *)0xDD07)
#define CIA2_ICR  (*(volatile uint8_t *)0xDD0D)
#define CIA2_CRA  (*(volatile uint8_t *)0xDD0E)
#define CIA2_CRB  (*(volatile uint8_t *)0xDD0F)

#define ATTIC     0x8100000UL       // where the game keeps its overlays
#define CHIP      0x40000UL         // the game's display buffer

// A busy loop, position independent: 256 x 256 dex/bne, then rts.
static uint8_t busy[] = {
    0xA0, 0x00,                     // ldy #0
    0xA2, 0x00,                     // ldx #0
    0xCA,                           // 1: dex
    0xD0, 0xFD,                     //    bne 1b
    0x88,                           //    dey
    0xD0, 0xFA,                     //    bne 1b
    0x60,                           //    rts
};

// Reads 64KB from the 32-bit address in hw_p (which must be 64KB-aligned).
__asm__(
    "  .zeropage hw_p\n"
    "  .section .zp.bss,\"aw\",@nobits\n"
    "  .globl hw_p\n"
    "hw_p: .zero 4\n"
    "  .section .text.hw_read64k,\"ax\",@progbits\n"
    "  .globl hw_read64k\n"
    "hw_read64k:\n"
    "  ldy #0\n"
    "1: ldz #0\n"
    "2: lda [hw_p],z\n"
    "  inz\n"
    "  bne 2b\n"
    "  inc hw_p+1\n"
    "  dey\n"
    "  bne 1b\n"
    "  ldz #0\n"
    "  rts\n");
extern uint32_t hw_p;
void hw_read64k(void);

static uint16_t ms(void)
{
    uint8_t hi, lo;
    do {
        hi = CIA2_TBHI;
        lo = CIA2_TBLO;
    } while (hi != CIA2_TBHI);
    return (uint16_t)~((uint16_t)hi << 8 | lo);     // counts up from 0
}

static void timer_start(void)
{
    CIA2_ICR = 0x7F;
    CIA2_CRA = 0;
    CIA2_CRB = 0;
    CIA2_TALO = 999 & 0xFF;         // 1 ms at 1 MHz
    CIA2_TAHI = 999 >> 8;
    CIA2_TBLO = 0xFF;
    CIA2_TBHI = 0xFF;
    CIA2_CRB = 0x51;                // count timer A underflows
    CIA2_CRA = 0x11;
}

// ---- the screen -------------------------------------------------------------------

static uint32_t screen;
static uint16_t linestep;
static uint8_t row;

static void put(uint8_t col, const char *s)
{
    uint32_t a = screen + (uint32_t)row * linestep + col;
    for (; *s; s++, a++) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c -= 'a' - 1;
        else if (c >= 'A' && c <= 'Z') c -= 'A' - 1;
        else if (c >= 0x60) c = ' ';
        m65_dma_fill(a, (uint8_t)c, 1);
        m65_debug_putc(*s);
    }
}

static void put_num(uint8_t col, uint16_t v, const char *unit)
{
    char buf[8];
    uint8_t i = 6;
    buf[7] = 0;
    buf[6] = ' ';
    do {
        buf[--i] = '0' + v % 10;
        v /= 10;
    } while (v && i);
    while (i) buf[--i] = ' ';
    put(col, buf);
    put(col + 7, unit);
}

static void text(const char *s)
{
    put(0, s);
    m65_debug_putc('\r');
    m65_debug_putc('\n');
    row++;
}

static void line(const char *label, uint16_t v, const char *unit)
{
    put(0, label);
    put_num(34, v, unit);
    m65_debug_putc('\r');
    m65_debug_putc('\n');
    row++;
}

int main(void)
{
    uint16_t t, i;

    __asm__ volatile("sei");
    // No MAP, ROMs out, full speed (as the game's m65_takeover).
    __asm__ volatile("lda #0\n ldx #0\n ldy #0\n ldz #0\n map\n eom\n" ::: "a", "x", "y");
    *(volatile uint8_t *)0x0001 = 0x35;
    *(volatile uint8_t *)0x0000 = 65;

    screen = *(volatile uint8_t *)0xD060
           | (uint32_t)*(volatile uint8_t *)0xD061 << 8
           | (uint32_t)*(volatile uint8_t *)0xD062 << 16
           | (uint32_t)(*(volatile uint8_t *)0xD063 & 0x0F) << 24;
    linestep = *(volatile uint8_t *)0xD058 | *(volatile uint8_t *)0xD059 << 8;
    m65_dma_fill(screen, ' ', 25 * linestep);
    m65_dma_fill(0xFF80000UL + (*(volatile uint8_t *)0xD064 | *(volatile uint8_t *)0xD065 << 8),
                 1, 25 * linestep);
    *(volatile uint8_t *)0xD020 = 0;
    *(volatile uint8_t *)0xD021 = 0;
    row = 1;
    text("wolf3d hardware timing (lower is faster)");
    row++;

    timer_start();

    // The clock against the video: 50 frames (the raster reaching line 0).
    {
        uint8_t n = 0, was = 1;
        t = ms();
        while (n < 50) {
            uint8_t top = *(volatile uint8_t *)0xD012 == 0
                       && !(*(volatile uint8_t *)0xD011 & 0x80);
            if (top && !was) n++;
            was = top;
        }
        line("50 video frames (want 1000):", ms() - t, "ms");
    }

    // The loop from chip RAM, 32 times.
    t = ms();
    for (i = 0; i < 32; i++)
        ((void (*)(void))(uintptr_t)busy)();
    line("code in chip ram, 32 loops:", ms() - t, "ms");

    // The same from attic RAM, mapped at $4000 as the game maps overlays.
    m65_dma_copy(ATTIC, (uint32_t)(uintptr_t)busy, sizeof busy);
    __asm__ volatile(
        "lda #$81\n ldx #$0f\n ldy #0\n ldz #0\n map\n eom\n"   // lower MB: $81
        "lda #$c0\n ldx #$4f\n ldy #0\n ldz #0\n map\n eom\n"   // $4000 -> $8100000
        ::: "a", "x", "y");
    t = ms();
    for (i = 0; i < 32; i++)
        ((void (*)(void))0x4000)();
    t = ms() - t;
    __asm__ volatile("lda #0\n ldx #0\n ldy #0\n ldz #0\n map\n eom\n" ::: "a", "x", "y");
    line("code in attic via $4000, 32 loops:", t, "ms");

    // Reading 64KB, 8 times.
    t = ms();
    for (i = 0; i < 8; i++) {
        hw_p = CHIP;
        hw_read64k();
    }
    line("read 8 x 64kb from chip ram:", ms() - t, "ms");
    t = ms();
    for (i = 0; i < 8; i++) {
        hw_p = ATTIC;
        hw_read64k();
    }
    line("read 8 x 64kb from attic ram:", ms() - t, "ms");

    // DMA, 8 copies of 64000 bytes.
    t = ms();
    for (i = 0; i < 8; i++)
        m65_dma_copy(CHIP + 0x10000, CHIP, 64000);
    line("dma 8 x 64000, chip to chip:", ms() - t, "ms");
    t = ms();
    for (i = 0; i < 8; i++)
        m65_dma_copy(CHIP + 0x10000, ATTIC, 64000);
    line("dma 8 x 64000, attic to chip:", ms() - t, "ms");
    t = ms();
    for (i = 0; i < 8; i++)
        m65_dma_fill(CHIP + 0x10000, 0, 64000);
    line("dma 8 x 64000 fill, chip:", ms() - t, "ms");

    row++;
    text("done. (reset to leave)");
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}
