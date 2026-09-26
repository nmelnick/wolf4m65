// Taking over the machine: the memory layout of the overlay build
// (tools/ovlgen.py's linker script) needs the KERNAL gone.
//
//   $0300-$1FFF  .rodata, copied here from its load address in the PRG
//   $2001-$3FFF  resident code
//   $4000-$7FFF  overlay window (at load time: the PRG's copy sources)
//   $8000-$CFFF  .data, .bss, heap, soft stack
//   $E000-$FFF9  .highbss (large arrays), zeroed here
//   $FFFA-$FFFF  our NMI/RESET/IRQ vectors
//
// This cannot run in the C runtime's start-up: that calls the KERNAL's CHROUT
// just before main.

#include <stdint.h>

#include "m65_platform.h"
#include "m65_video.h"

extern char __rodata_start[], __rodata_load_start[], __rodata_size[];
extern char __highbss_start[], __highbss_size[];

// NMI (RESTORE key): ignore. IRQ/BRK: interrupts are off, so this is a BRK,
// i.e. a jump into zeroed memory: report it and stop with a red border.
__asm__(
    ".section .text.m65_vectors,\"ax\",@progbits\n"
    "m65_nmi:\n"
    "    rti\n"
    "m65_irq:\n"
    "    sei\n"
    "    lda #'B'\n sta $d643\n clv\n"
    "    lda #'R'\n sta $d643\n clv\n"
    "    lda #'K'\n sta $d643\n clv\n"
    "    lda #13\n sta $d643\n clv\n"
    "    lda #10\n sta $d643\n clv\n"
    "1:  lda #2\n sta $d020\n"
    "    jmp 1b\n");

extern char m65_nmi[], m65_irq[];

void m65_takeover(void)
{
    __asm__ volatile("sei");

    // No MAPping, and the C64-style ROMs banked out: RAM at $A000-$BFFF and
    // $E000-$FFFF, I/O at $D000.
    __asm__ volatile("lda #0\n ldx #0\n ldy #0\n ldz #0\n map\n eom\n" ::: "a", "x", "y");
    *(volatile uint8_t *)0x0001 = 0x35;

    *(volatile uint16_t *)0xFFFA = (uint16_t)(uintptr_t)m65_nmi;
    *(volatile uint16_t *)0xFFFC = (uint16_t)(uintptr_t)m65_irq;
    *(volatile uint16_t *)0xFFFE = (uint16_t)(uintptr_t)m65_irq;

    if ((uint16_t)(uintptr_t)__rodata_size)
        m65_dma_copy((uint32_t)(uintptr_t)__rodata_start,
                     (uint32_t)(uintptr_t)__rodata_load_start,
                     (uint16_t)(uintptr_t)__rodata_size);
    if ((uint16_t)(uintptr_t)__highbss_size)
        m65_dma_fill((uint32_t)(uintptr_t)__highbss_start, 0,
                     (uint16_t)(uintptr_t)__highbss_size);
}
