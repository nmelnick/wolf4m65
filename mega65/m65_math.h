// Arithmetic on the MEGA65's math unit (m65_math.c).
#ifndef M65_MATH_H
#define M65_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wolf3D's FixedMul (16.16 fixed point), exactly, on the hardware multiplier.
int32_t m65_fixedmul(int32_t a, int32_t b);

#ifdef __cplusplus
}
#endif

#endif
