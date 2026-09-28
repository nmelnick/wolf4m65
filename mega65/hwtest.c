// Hardware timing test: what costs what on this machine (make hwtest.prg).
//
// Times, with the CIA 2 millisecond timer the game uses (SDL_GetTicks):
//   - the timer itself, against 50 video frames (1000 ms on PAL);
//   - a busy loop run from chip RAM, and the same loop run from attic RAM
//     through the overlay window at $4000 (how the game runs its code);
//   - reading 64KB from chip RAM and from attic RAM (32-bit pointer);
//   - DMA copies of 64000 bytes, chip to chip and attic to chip.
//   - the game's multiply and divide routines on the math unit (m65_math.c,
//     m65_hwdiv.c) against a plain shift-and-subtract reference.
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

// ---- the math unit routines, against a reference --------------------------------

uint32_t m65_udivmod(uint32_t n, uint32_t d);
uint32_t __udivsi3(uint32_t, uint32_t);
uint32_t __umodsi3(uint32_t, uint32_t);
int32_t __divsi3(int32_t, int32_t);
int32_t __modsi3(int32_t, int32_t);
uint16_t __udivhi3(uint16_t, uint16_t);
int16_t __divhi3(int16_t, int16_t);
int16_t __modhi3(int16_t, int16_t);
uint32_t __mulsi3(uint32_t, uint32_t);
uint16_t __mulhi3(uint16_t, uint16_t);

// n / d and n % d by shift and subtract (no library calls).
static uint32_t ref_rem;
static uint32_t ref_div(uint32_t n, uint32_t d)
{
    uint32_t q = 0, r = 0;
    int8_t i;
    for (i = 31; i >= 0; i--) {
        r = r << 1 | (n >> i & 1);
        if (r >= d) {
            r -= d;
            q |= 1UL << i;
        }
    }
    ref_rem = r;
    return q;
}

static uint32_t ref_mul(uint32_t a, uint32_t b)
{
    uint32_t p = 0;
    while (b) {
        if (b & 1)
            p += a;
        a <<= 1;
        b >>= 1;
    }
    return p;
}

static uint32_t seed = 12345;
static uint32_t rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

// One pair through every routine; returns the number of wrong results.
static uint8_t check(uint32_t a, uint32_t b)
{
    uint8_t bad = 0;
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    uint32_t q, r, ua, ub;

    if (__mulsi3(a, b) != ref_mul(a, b)) bad++;
    if (__mulhi3((uint16_t)a, (uint16_t)b) != (uint16_t)ref_mul(a & 0xFFFF, b & 0xFFFF)) bad++;
    if (!b)
        return bad;
    q = ref_div(a, b);
    if (__udivsi3(a, b) != q || __umodsi3(a, b) != ref_rem) bad++;
    if (sb != -1 || sa != (int32_t)0x80000000) {        // (overflow: undefined)
        ua = sa < 0 ? -(uint32_t)sa : (uint32_t)sa;
        ub = sb < 0 ? -(uint32_t)sb : (uint32_t)sb;
        q = ref_div(ua, ub);
        r = ref_rem;
        if ((sa < 0) != (sb < 0)) q = -q;
        if (sa < 0) r = -r;
        if ((uint32_t)__divsi3(sa, sb) != q || (uint32_t)__modsi3(sa, sb) != r) bad++;
    }
    if ((uint16_t)b) {
        q = ref_div(a & 0xFFFF, b & 0xFFFF);
        if (__udivhi3((uint16_t)a, (uint16_t)b) != (uint16_t)q) bad++;
        if ((int16_t)b != -1 || (int16_t)a != -32768) {
            int16_t x = (int16_t)a, y = (int16_t)b;
            uint16_t ux = x < 0 ? -(uint16_t)x : (uint16_t)x, uy = y < 0 ? -(uint16_t)y : (uint16_t)y;
            uint16_t q16 = (uint16_t)ref_div(ux, uy), r16 = (uint16_t)ref_rem;
            if ((x < 0) != (y < 0)) q16 = -q16;
            if (x < 0) r16 = -r16;
            if ((uint16_t)__divhi3(x, y) != q16 || (uint16_t)__modhi3(x, y) != r16) bad++;
        }
    }
    return bad;
}

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

    // The same from attic RAM, mapped in as the game maps overlays (at
    // $6000 here: this program has grown past $4000, where the game maps).
    m65_dma_copy(ATTIC, (uint32_t)(uintptr_t)busy, sizeof busy);
    __asm__ volatile(
        "lda #$81\n ldx #$0f\n ldy #0\n ldz #0\n map\n eom\n"   // lower MB: $81
        "lda #$a0\n ldx #$8f\n ldy #0\n ldz #0\n map\n eom\n"   // $6000 -> $8100000
        ::: "a", "x", "y");
    t = ms();
    for (i = 0; i < 32; i++)
        ((void (*)(void))0x6000)();
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

    // DMA jobs as the renderer issues them. Setup cost: 3200 one-byte jobs
    // (about ten frames' worth of wall columns).
    t = ms();
    for (i = 0; i < 3200; i++)
        m65_dma_scale(CHIP + 0x10000 + i, CHIP, 1, 0x100, 1);
    line("dma 3200 one-byte jobs:", ms() - t, "ms");

    // Ten frames of wall columns: 304 columns of 120 pixels, each scaled
    // from a 64-byte texture column (step 64/120), every 8th byte (the
    // screen's layout), from the three places textures could live.
    {
        static const uint32_t from[3] = { ATTIC, CHIP, 0xFF80800UL };
        static const char *const what[3] = {
            "10 frames of walls from attic:", "10 frames of walls from chip:",
            "10 frames of walls from colour ram:" };
        uint8_t k, f;
        for (k = 0; k < 3; k++) {
            t = ms();
            for (f = 0; f < 10; f++)
                for (i = 0; i < 304; i++)
                    m65_dma_scale(CHIP + 0x10000 + i, from[k] + (i & 63) * 64,
                                  120, 0x88, 8);
            line(what[k], ms() - t, "ms");
        }
    }

    // Colour RAM as the game's texture cache uses it: seven 4KB slots from
    // $FF81000, each written with its own pattern and read back by DMA.
    // A count of bytes that did not come back (0: good).
    {
        static uint8_t pat[256], back[256];
        static char label[] = "colour ram $ff8.000 bad bytes:";
        uint8_t sl, j;
        uint16_t bad, o;
        for (sl = 1; sl <= 7; sl++) {
            uint32_t base = 0xFF80000UL + ((uint32_t)sl << 12);
            for (j = 0; ; j++) {
                pat[j] = (uint8_t)(j * 7 + sl * 31);
                if (j == 255) break;
            }
            for (o = 0; o < 4096; o += 256)
                m65_dma_copy(base + o, (uint32_t)(uintptr_t)pat, 256);
            bad = 0;
            for (o = 0; o < 4096; o += 256) {
                m65_dma_copy((uint32_t)(uintptr_t)back, base + o, 256);
                for (j = 0; ; j++) {
                    if (back[j] != pat[j]) bad++;
                    if (j == 255) break;
                }
            }
            label[15] = "0123456789abcdef"[sl];
            line(label, bad, "");
        }
        // A scaled read (as the wall columns are drawn: step 64/120) from
        // the last slot, against the same from chip RAM holding the same.
        for (o = 0; o < 4096; o += 256)
            m65_dma_copy(CHIP + 0x20000 + o, (uint32_t)(uintptr_t)pat, 256);
        m65_dma_scale(CHIP + 0x21000, 0xFF87000UL + 64, 120, 0x88, 1);
        m65_dma_scale(CHIP + 0x21100, CHIP + 0x20000 + 64, 120, 0x88, 1);
        m65_dma_copy((uint32_t)(uintptr_t)back, CHIP + 0x21000, 120);
        m65_dma_copy((uint32_t)(uintptr_t)pat, CHIP + 0x21100, 120);
        bad = 0;
        for (j = 0; j < 120; j++)
            if (back[j] != pat[j]) bad++;
        line("scaled read from colour ram, bad:", bad, "");
        // Filled as the game fills it: attic RAM to colour RAM, 4KB at once.
        for (j = 0; ; j++) {
            pat[j] = (uint8_t)(j ^ 0xA5);
            if (j == 255) break;
        }
        for (o = 0; o < 4096; o += 256)
            m65_dma_copy(ATTIC + 0x10000 + o, (uint32_t)(uintptr_t)pat, 256);
        m65_dma_copy(0xFF87000UL, ATTIC + 0x10000, 4096);
        bad = 0;
        for (o = 0; o < 4096; o += 256) {
            m65_dma_copy((uint32_t)(uintptr_t)back, 0xFF87000UL + o, 256);
            for (j = 0; ; j++) {
                if (back[j] != pat[j]) bad++;
                if (j == 255) break;
            }
        }
        line("attic to colour ram copy, bad:", bad, "");
    }

    // The multiply and divide routines: edge cases, then random pairs of
    // assorted sizes.
    {
        static const uint32_t edge[] = {
            0, 1, 2, 3, 7, 10, 255, 256, 65535, 65536, 0x7FFFFFFFUL, 0x80000000UL,
            0xFFFFFFFFUL, 0xFFFFFFFEUL, 1000000UL, 0x12345678UL, 0xFFFF0000UL };
        uint16_t bad = 0, n = 0, k;
        uint8_t i, j;
        put(0, "math unit mul/div: checking...");      // (overwritten when done)
        for (i = 0; i < sizeof edge / sizeof edge[0]; i++)
            for (j = 0; j < sizeof edge / sizeof edge[0]; j++, n++)
                bad += check(edge[i], edge[j]);
        t = ms();
        for (k = 0; k < 20000; k++, n++) {
            uint32_t a = rnd() >> (rnd() & 31), b = rnd() >> (rnd() & 31);
            bad += check(a, b);
            if ((k & 1023) == 0)            // progress: a dot per 1024 pairs
                put(31 + (k >> 10), ".");
        }
        put(0, "                                                            ");
        line("math unit mul/div: wrong results", bad, "");
        line("  (pairs checked)", n, "");
    }

    row++;
    text("done. (reset to leave)");
    m65_debug_puts("TEST-DONE");
    for (;;) {}
}
