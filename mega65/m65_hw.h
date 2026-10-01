// MEGA65 I/O registers that more than one file uses (those only one file
// uses are defined there). User guide: "MEGA65 I/O registers".

#ifndef M65_HW_H
#define M65_HW_H

#include <stdint.h>

// VIC: border colour, raster interrupt flags (write 1s to clear) and mask.
#define VIC_BORDER   (*(volatile uint8_t *)0xD020)
#define VIC_IRQ      (*(volatile uint8_t *)0xD019)
#define VIC_IRQMASK  (*(volatile uint8_t *)0xD01A)

// CIA 1: the joystick ports, and timer A (the sound's and the profiler's
// interrupt). The CIAs count at 1.000 MHz, not at the C64's PAL clock
// (measured against the video frame rate in Xemu: titletest).
#define CIA_HZ       1000000UL
#define CIA1_PRA     (*(volatile uint8_t *)0xDC00)
#define CIA1_PRB     (*(volatile uint8_t *)0xDC01)
#define CIA1_DDRA    (*(volatile uint8_t *)0xDC02)
#define CIA1_DDRB    (*(volatile uint8_t *)0xDC03)
#define CIA1_TALO    (*(volatile uint8_t *)0xDC04)
#define CIA1_TAHI    (*(volatile uint8_t *)0xDC05)
#define CIA1_ICR     (*(volatile uint8_t *)0xDC0D)
#define CIA1_CRA     (*(volatile uint8_t *)0xDC0E)

// The math unit: the 32 x 32 -> 64 bit multiplier (the product is there
// straight away) and the divider (DIVOUT, 32.32 fixed point, is ready once
// MATH_BUSY bit 7 clears), both from MULTINA and MULTINB.
#define MATH_BUSY    (*(volatile uint8_t *)0xD70F)
#define MATH_DIVOUT_INT (*(volatile uint16_t *)0xD76C)  // the quotient's integer part (low 16 bits)
#define MATH_MULTINA (*(volatile uint32_t *)0xD770)
#define MATH_MULTINB (*(volatile uint32_t *)0xD774)
#define MATH_MULTOUT (*(volatile uint32_t *)0xD778)     // the product's bits 0-31

#endif
