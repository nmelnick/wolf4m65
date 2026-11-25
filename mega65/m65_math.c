// Arithmetic on the MEGA65's math unit (resident: every overlay uses it).
//
// The 32 x 32 -> 64 bit multiplier (user guide, "Math Acceleration"):
// write the factors to MULTINA ($D770) and MULTINB ($D774), and the product
// is at MULTOUT ($D778) straight away.

#include <stdint.h>

#include "m65_hw.h"
#include "m65_math.h"

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
    MATH_MULTINA = ua;
    MATH_MULTINB = ub;
    lo = MULTOUT_L;
    hi = MULTOUT_M;
    round = neg ? 0x7FFF : 0x8000;
    if ((uint16_t)(lo + round) < lo)
        hi++;
    return neg ? -(int32_t)hi : (int32_t)hi;
}

// The compiler's 16- and 32-bit multiplies (llvm-mos's __mulhi3/__mulsi3,
// which tools/libsplit.py takes out of the library), on the multiplier: the
// low bits of the product are the same signed or unsigned. Calling
// convention: the first factor in A, X (low first), __rc2, __rc3; the
// second from __rc2 (16-bit) or __rc4 (32-bit); the product in A, X, __rc2,
// __rc3. The unused high input bytes are written as 0.
__asm__(
    ".section .text.__mulhi3,\"ax\",@progbits\n"
    ".globl __mulhi3\n"
    ".type __mulhi3,@function\n"
    "__mulhi3:\n"
    "  sta $d770\n stx $d771\n"
    "  lda __rc2\n sta $d774\n lda __rc3\n sta $d775\n"
    "  lda #0\n sta $d772\n sta $d773\n sta $d776\n sta $d777\n"
    "  lda $d778\n ldx $d779\n"
    "  rts\n"
    ".size __mulhi3, . - __mulhi3\n"
    ".section .text.__mulsi3,\"ax\",@progbits\n"
    ".globl __mulsi3\n"
    ".type __mulsi3,@function\n"
    "__mulsi3:\n"
    "  sta $d770\n stx $d771\n"
    "  lda __rc2\n sta $d772\n lda __rc3\n sta $d773\n"
    "  lda __rc4\n sta $d774\n lda __rc5\n sta $d775\n"
    "  lda __rc6\n sta $d776\n lda __rc7\n sta $d777\n"
    "  lda $d77a\n sta __rc2\n lda $d77b\n sta __rc3\n"
    "  lda $d778\n ldx $d779\n"
    "  rts\n"
    ".size __mulsi3, . - __mulsi3\n");
