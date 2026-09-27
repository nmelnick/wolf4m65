// Taking over the machine: the memory layout of the overlay build
// (tools/ovlgen.py's linker script) needs the KERNAL gone.
//
//   $0300-$1FFF  .rodata (copied here from its load address in the PRG),
//                then .lowbss (zeroed here)
//   $2001-$3FFF  resident code, then .prgbss (zeroed here)
//   $4000-$7FFF  overlay window (at load time: the PRG's copy sources)
//   $8000-$CFFF  .data, .bss, heap, soft stack
//   $E000-$FFF9  .highbss (large arrays), zeroed here
//   $FFFA-$FFFF  our NMI/RESET/IRQ vectors
//
// This cannot run in the C runtime's start-up: that calls the KERNAL's CHROUT
// just before main.

#include <stdint.h>
#include <string.h>

#include "m65_debug.h"
#include "m65_dos.h"
#include "m65_platform.h"
#include "ovl/ovl_load.h"
#include "m65_video.h"

extern char __rodata_start[], __rodata_load_start[], __rodata_size[];
extern char __highbss_start[], __highbss_size[];
extern char __lowbss_start[], __lowbss_size[];
extern char __prgbss_start[], __prgbss_size[];

// NMI (RESTORE key): ignore. IRQ/BRK: a BRK (the timer interrupts have
// their own handlers, which pass BRKs on here), i.e. a jump into zeroed
// memory: report it and stop with a red border. The stack is anywhere (16-bit
// stack pointer): the pushed return address is read through m65_irq_p.
__asm__(
    ".zeropage m65_irq_p\n"
    ".section .zp.bss,\"aw\",@nobits\n"
    "m65_irq_p: .zero 2\n"
    ".section .text.m65_vectors,\"ax\",@progbits\n"
    "m65_nmi:\n"
    "    rti\n"
    ".globl m65_irq\n"
    "m65_irq:\n"
    "    sei\n"
    "    lda #'B'\n sta $d643\n clv\n"
    "    lda #'R'\n sta $d643\n clv\n"
    "    lda #'K'\n sta $d643\n clv\n"
    "    lda #' '\n sta $d643\n clv\n"
    // The return address pushed by BRK (the BRK's address + 2), in hex.
    "    tsx\n stx m65_irq_p\n tsy\n sty m65_irq_p+1\n"
    "    ldy #3\n lda (m65_irq_p),y\n jsr m65_hex\n"
    "    ldy #2\n lda (m65_irq_p),y\n jsr m65_hex\n"
    "    lda #13\n sta $d643\n clv\n"
    "    lda #10\n sta $d643\n clv\n"
    "1:  lda #2\n sta $d020\n"
    "    jmp 1b\n"
    "m65_hex:\n"
    "    pha\n lsr\n lsr\n lsr\n lsr\n jsr 2f\n pla\n and #15\n"
    "2:  cmp #10\n bcc 3f\n adc #6\n"
    "3:  adc #'0'\n sta $d643\n clv\n rts\n");

extern char m65_nmi[], m65_irq[];

// The CPU stack's page (SPH) and pointer when the program started, and where
// it moves to (see m65_takeover).
uint8_t m65_boot_sph, m65_boot_spl;
uint16_t m65_new_sp;
extern char __hwstack_top[];

void m65_takeover(void)
{
    __asm__ volatile("sei");

    // The hardware stack: it starts in page $01 with about 225 bytes left,
    // and the CPU runs it in 16-bit mode, so running out does not wrap in
    // the page but goes on down through the zero page (the compiler's
    // registers). With overlays each call across them costs 5 more bytes,
    // and deep call chains did run out. Move it to the 512 bytes at the top
    // of mid (__hwstack_top, see ovlgen.py): copy what is on it (return
    // addresses and all) and point the stack pointer there, in 16-bit mode.
    __asm__ volatile("tsy\n sty m65_boot_sph\n tsx\n stx m65_boot_spl\n" ::: "x", "y");
    {
        uint16_t sp = (uint16_t)m65_boot_sph << 8 | m65_boot_spl;
        uint16_t used = ((uint16_t)m65_boot_sph << 8 | 0xFF) - sp;
        m65_new_sp = (uint16_t)(uintptr_t)__hwstack_top - used;
        m65_dma_copy(m65_new_sp + 1, sp + 1, used);
        __asm__ volatile("ldx m65_new_sp\n txs\n ldy m65_new_sp+1\n tys\n cle\n" ::: "x", "y");
    }

    // No MAPping, and the C64-style ROMs banked out: RAM at $A000-$BFFF and
    // $E000-$FFFF, I/O at $D000.
    __asm__ volatile("lda #0\n ldx #0\n ldy #0\n ldz #0\n map\n eom\n" ::: "a", "x", "y");
    *(volatile uint8_t *)0x0001 = 0x35;

    // Full speed (40MHz): 65 to the CPU port's $00 (user guide,
    // appendix-dmagic.tex: "LDA #65 ; Set CPU speed to fast / STA 0").
    *(volatile uint8_t *)0x0000 = 65;

    *(volatile uint16_t *)0xFFFA = (uint16_t)(uintptr_t)m65_nmi;
    *(volatile uint16_t *)0xFFFC = (uint16_t)(uintptr_t)m65_irq;
    *(volatile uint16_t *)0xFFFE = (uint16_t)(uintptr_t)m65_irq;

    // (Empty regions are fine: the DMA helpers ignore a count of 0.)
    m65_dma_copy((uint32_t)(uintptr_t)__rodata_start,
                 (uint32_t)(uintptr_t)__rodata_load_start,
                 (uint16_t)(uintptr_t)__rodata_size);
    m65_dma_fill((uint32_t)(uintptr_t)__highbss_start, 0,
                 (uint16_t)(uintptr_t)__highbss_size);
    m65_dma_fill((uint32_t)(uintptr_t)__lowbss_start, 0,
                 (uint16_t)(uintptr_t)__lowbss_size);
    m65_dma_fill((uint32_t)(uintptr_t)__prgbss_start, 0,
                 (uint16_t)(uintptr_t)__prgbss_size);
}

extern char __m65_data_start[], __m65_data_size[];

// Write an ASCII string to the text screen at (row, column 0) as screen
// codes (the lower case character set: a-z are 1-26, A-Z as in ASCII).
static void screen_puts(uint32_t screen, uint16_t linestep, uint8_t row, const char *s)
{
    uint32_t a = screen + (uint32_t)row * linestep;
    for (; *s; s++, a++) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 1;
        else if (c >= 0x5B)
            c = ' ';
        m65_dma_fill(a, (uint8_t)c, 1);
    }
}

// A text screen of our own, for start-up (until the game sets its video
// mode): the one the program was started from shows garbage once the
// takeover copies the program's data over its memory ($0800) and the
// overlays go over the character set (in the ROM area, $2xxxx). So the
// font is copied to TEXT_FONT (the sound effects' rings, m65_sd.cpp: free
// until the sound starts), and the screen moved to TEXT_SCREEN (the game's
// screen RAM, M65_SCREENRAM: free until the video mode is set); cleared.
#define TEXT_FONT   0x11000UL
#define TEXT_SCREEN 0x12000UL
#define VIC(r)      (*(volatile uint8_t *)(0xD000 + (r)))

static uint16_t text_linestep;

static void text_screen(void)
{
    uint32_t font = VIC(0x68) | (uint16_t)VIC(0x69) << 8 | (uint32_t)VIC(0x6A) << 16;

    if (font != TEXT_FONT) {
        // (A C64-style $1000 or $1800 means the character ROM, at $29000:
        // its lower case set, at $29800, for mixed case.)
        if (font < 0x10000UL)
            font = 0x29800UL;
        m65_dma_copy(TEXT_FONT, font, 2048);
        VIC(0x68) = (uint8_t)TEXT_FONT;
        VIC(0x69) = (uint8_t)(TEXT_FONT >> 8);
        VIC(0x6A) = (uint8_t)(TEXT_FONT >> 16);
    }
    VIC(0x60) = (uint8_t)TEXT_SCREEN;
    VIC(0x61) = (uint8_t)(TEXT_SCREEN >> 8);
    VIC(0x62) = (uint8_t)(TEXT_SCREEN >> 16);
    VIC(0x63) &= 0xF0;
    text_linestep = VIC(0x58) | VIC(0x59) << 8;
    m65_dma_fill(TEXT_SCREEN, ' ', 25 * text_linestep);
    m65_dma_fill(0xFF80000UL + (VIC(0x64) | VIC(0x65) << 8), 1,   // colour RAM: white
                 25 * text_linestep);
    VIC(0x20) = 0;
    VIC(0x21) = 0;
}

// Text centred on row `row`.
static void text_centre(uint8_t row, const char *s)
{
    screen_puts(TEXT_SCREEN + (text_linestep - strlen(s)) / 2, text_linestep, row, s);
}

// Report a start-up failure on the debug serial port and on the screen,
// then stop with a red border.
static void fail(const char *what, const char *file)
{
    m65_debug_puts(what);
    m65_debug_puts(file);
    text_screen();
    screen_puts(TEXT_SCREEN, text_linestep, 1, what);
    screen_puts(TEXT_SCREEN, text_linestep, 2, file);
    screen_puts(TEXT_SCREEN, text_linestep, 4, "Copy the WOLF4M65 folder to the SD card,");
    screen_puts(TEXT_SCREEN, text_linestep, 5, "with the game's *.WL1 files in it.");
    for (;;)
        VIC(0x20) = 2;
}

#ifdef M65_RESIDENT
// The game's directory on the SD card (m65_dos.c), used from start-up on,
// before WOLF.DAT is loaded: so in plain .rodata, loaded with the PRG (named
// .rodata.* sections are in .rodata_far, from WOLF.DAT: see ovlgen.py).
__attribute__((section(".rodata"))) const char m65_dos_subdir[] = "WOLF4M65";
#endif

void m65_startup(const char *ovlfile, const char *datafile)
{
    uint16_t size = (uint16_t)(uintptr_t)__m65_data_size;
    uint16_t at = 0, got;
    int fd = -1;

    m65_takeover();
    text_screen();
    text_centre(12, "Wolfenstein 3-D is loading...");

    // .data comes from its own file (it is not in the PRG). (Sector by
    // sector here: m65_dos_read is in .midtext, which this loads.)
    if (size) {
        if (m65_dos_init() != 0 || (fd = m65_dos_open(datafile)) < 0)
            fail("Cannot open the data file:", datafile);
        while (at < size) {
            got = m65_dos_read512(fd, (uint32_t)(uintptr_t)__m65_data_start + at);
            if (got == 0 || got == M65_DOS_EOF)
                break;
            at += got;
        }
        if (at != size)
            fail("The data file does not match the program:", datafile);
        m65_dos_close(fd);
    }

    if (ovl_load(ovlfile) != 0)
        fail("Cannot load the code overlays:", ovlfile);
}
