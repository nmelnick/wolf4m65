// Arithmetic on the MEGA65's math unit (m65_math.c).
#ifndef M65_MATH_H
#define M65_MATH_H

#include <stdint.h>

#include "m65_hw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wolf3D's FixedMul (16.16 fixed point), exactly, on the hardware multiplier.
int32_t m65_fixedmul(int32_t a, int32_t b);

// n / d straight from the math unit's divider, for drawing: its integer part
// can be one off (m65_hwdiv.c puts that right for the C operators; a texture
// step or texel does not need it), and it takes a few cycles.
static inline uint16_t DivApprox (uint32_t n, uint16_t d)
{
    MATH_MULTINA = n;
    MATH_MULTINB = d;
    while(MATH_BUSY & 0x80)
        ;
    return MATH_DIVOUT_INT;
}

// a * b on the math unit (16 x 16 -> 32 bits; __mulsi3 is 32 x 32, and a call).
static inline uint32_t HwMul16 (uint16_t a, uint16_t b)
{
    volatile uint8_t *m = (volatile uint8_t *) &MATH_MULTINA;
    m[0] = (uint8_t) a; m[1] = (uint8_t) (a >> 8); m[2] = 0; m[3] = 0;
    m[4] = (uint8_t) b; m[5] = (uint8_t) (b >> 8); m[6] = 0; m[7] = 0;
    return MATH_MULTOUT;
}

#ifdef __cplusplus
}
#endif

#endif
