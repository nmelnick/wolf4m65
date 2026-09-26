// Arithmetic on the MEGA65's math unit (resident: every overlay uses it).
//
// The 32 x 32 -> 64 bit multiplier (user guide, "Math Acceleration"):
// write the factors to MULTINA ($D770) and MULTINB ($D774), and the product
// is at MULTOUT ($D778) straight away.

#include <stdint.h>

#include "m65_math.h"

#define MULTINA   (*(volatile uint32_t *)0xD770)
#define MULTINB   (*(volatile uint32_t *)0xD774)
#define MULTOUT_L (*(volatile uint16_t *)0xD778)   // bits 0-15
#define MULTOUT_M (*(volatile uint32_t *)0xD77A)   // bits 16-47

// Wolf3D's FixedMul: (int32_t)(((int64_t)a * b + 0x8000) >> 16), exactly.
// The multiplier is unsigned, so this multiplies the magnitudes; for a
// negative product p = -|a*b|, (p + 0x8000) >> 16 (a flooring shift) is
// -((|a*b| + 0x7FFF) >> 16).
int32_t m65_fixedmul(int32_t a, int32_t b)
{
    uint8_t neg = 0;
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b, hi;
    uint16_t lo, round;

    if (a < 0) {
        ua = -ua;
        neg = 1;
    }
    if (b < 0) {
        ub = -ub;
        neg ^= 1;
    }
    MULTINA = ua;
    MULTINB = ub;
    lo = MULTOUT_L;
    hi = MULTOUT_M;
    round = neg ? 0x7FFF : 0x8000;
    if ((uint16_t)(lo + round) < lo)
        hi++;
    return neg ? -(int32_t)hi : (int32_t)hi;
}
