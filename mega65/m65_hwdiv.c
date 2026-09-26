// The compiler's 16- and 32-bit divisions and remainders (llvm-mos's
// __udivsi3 and friends, which tools/libsplit.py takes out of the library),
// on the math unit's divider: write the numerator to MULTINA ($D770) and
// the denominator to MULTINB ($D774), wait while DIVBUSY ($D70F bit 7), and
// DIVOUT ($D768) is the 32.32 fixed-point quotient.
//
// The divider iterates (Goldschmidt) and rounds; its integer part can be one
// off (the core adds a fudge "to stop things like 4/2 giving 1.999999999").
// So the quotient is checked with the multiplier (q * d against n) and put
// right, which makes these exact: the same results as the C operators.

#include <stdint.h>

#define MULTINA   (*(volatile uint32_t *)0xD770)
#define MULTINB   (*(volatile uint32_t *)0xD774)
#define MULTOUT_L (*(volatile uint32_t *)0xD778)
#define MULTOUT_H (*(volatile uint32_t *)0xD77C)
#define DIVOUT_H  (*(volatile uint32_t *)0xD76C)    // the integer part
#define DIVBUSY   (*(volatile uint8_t *)0xD70F)

static uint32_t rem;

// n / d, and the remainder in rem (d = 0: all ones, remainder n).
uint32_t m65_udivmod(uint32_t n, uint32_t d)
{
    uint32_t q, plo, phi;

    if (!d) {
        rem = n;
        return 0xFFFFFFFFUL;
    }
    MULTINA = n;
    MULTINB = d;
    while (DIVBUSY & 0x80)
        ;
    q = DIVOUT_H;
    MULTINA = q;                        // q * d (d is still in MULTINB)
    plo = MULTOUT_L;
    phi = MULTOUT_H;
    while (phi || plo > n) {            // q too big
        q--;
        if (plo < d)
            phi--;
        plo -= d;
    }
    while (n - plo >= d) {              // q too small
        q++;
        plo += d;
    }
    rem = n - plo;
    return q;
}

uint32_t __udivsi3(uint32_t a, uint32_t b) { return m65_udivmod(a, b); }
uint32_t __umodsi3(uint32_t a, uint32_t b) { m65_udivmod(a, b); return rem; }

// Signed: the magnitudes, then the C signs (the quotient truncates, the
// remainder has the sign of the dividend).
int32_t __divsi3(int32_t a, int32_t b)
{
    uint32_t q = m65_udivmod(a < 0 ? -(uint32_t)a : (uint32_t)a,
                             b < 0 ? -(uint32_t)b : (uint32_t)b);
    return (a < 0) != (b < 0) ? -(int32_t)q : (int32_t)q;
}

int32_t __modsi3(int32_t a, int32_t b)
{
    m65_udivmod(a < 0 ? -(uint32_t)a : (uint32_t)a,
                b < 0 ? -(uint32_t)b : (uint32_t)b);
    return a < 0 ? -(int32_t)rem : (int32_t)rem;
}

uint16_t __udivhi3(uint16_t a, uint16_t b) { return (uint16_t)m65_udivmod(a, b); }
uint16_t __umodhi3(uint16_t a, uint16_t b) { m65_udivmod(a, b); return (uint16_t)rem; }
int16_t __divhi3(int16_t a, int16_t b) { return (int16_t)__divsi3(a, b); }
int16_t __modhi3(int16_t a, int16_t b) { return (int16_t)__modsi3(a, b); }
