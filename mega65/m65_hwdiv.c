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

// n / d (d = 0: all ones), and the remainder n % d in m65_rem.
// Calling convention (llvm-mos): n in A, X, __rc2, __rc3; d in __rc4..__rc7;
// the quotient back in A, X, __rc2, __rc3. Uses __rc8..__rc15 (caller-saved).
uint32_t m65_rem;
uint32_t m65_udivmod(uint32_t n, uint32_t d);
__asm__(
    ".section .text.m65_udivmod,\"ax\",@progbits\n"
    ".globl m65_udivmod\n"
    ".type m65_udivmod,@function\n"
    "m65_udivmod:\n"
    // n: to the divider, and kept in __rc8..11
    "  sta $d770\n sta __rc8\n"
    "  stx $d771\n stx __rc9\n"
    "  lda __rc2\n sta $d772\n sta __rc10\n"
    "  lda __rc3\n sta $d773\n sta __rc11\n"
    "  lda __rc4\n ora __rc5\n ora __rc6\n ora __rc7\n"
    "  bne 1f\n"
    "  ldx #3\n"                              // d = 0: rem = n, q = all ones
    "0: lda __rc8,x\n sta m65_rem,x\n dex\n bpl 0b\n"
    "  lda #$ff\n sta __rc2\n sta __rc3\n tax\n rts\n"
    "1: lda __rc4\n sta $d774\n lda __rc5\n sta $d775\n"
    "  lda __rc6\n sta $d776\n lda __rc7\n sta $d777\n"
    "2: bit $d70f\n bmi 2b\n"                 // DIVBUSY
    // q (the integer part) to __rc12..15, and to the multiplier: q * d
    "  lda $d76c\n sta __rc12\n sta $d770\n"
    "  lda $d76d\n sta __rc13\n sta $d771\n"
    "  lda $d76e\n sta __rc14\n sta $d772\n"
    "  lda $d76f\n sta __rc15\n sta $d773\n"
    // p = q * d: the low half to m65_rem (working), the high half's bytes
    // ORed into __rc2 (non-zero: far too big)
    "  ldx #3\n"
    "3: lda $d778,x\n sta m65_rem,x\n dex\n bpl 3b\n"
    "  lda $d77c\n ora $d77d\n ora $d77e\n ora $d77f\n sta __rc2\n"
    // while (p > n): q--, p -= d
    "4: lda __rc2\n bne 5f\n"
    "  lda __rc11\n cmp m65_rem+3\n bne 6f\n"
    "  lda __rc10\n cmp m65_rem+2\n bne 6f\n"
    "  lda __rc9\n cmp m65_rem+1\n bne 6f\n"
    "  lda __rc8\n cmp m65_rem\n"
    "6: bcs 7f\n"                             // n >= p: done with this
    "5: lda __rc12\n bne 8f\n"                // q--
    "  lda __rc13\n bne 9f\n"
    "  lda __rc14\n bne 10f\n"
    "  dec __rc15\n"
    "10: dec __rc14\n"
    "9: dec __rc13\n"
    "8: dec __rc12\n"
    "  sec\n"                                 // p -= d (borrow into the high half)
    "  lda m65_rem\n sbc __rc4\n sta m65_rem\n"
    "  lda m65_rem+1\n sbc __rc5\n sta m65_rem+1\n"
    "  lda m65_rem+2\n sbc __rc6\n sta m65_rem+2\n"
    "  lda m65_rem+3\n sbc __rc7\n sta m65_rem+3\n"
    "  bcs 4b\n"
    "  lda #0\n sta __rc2\n"                  // (a borrow: the high half is now 0)
    "  bra 4b\n"
    // rem = n - p; while (rem >= d): q++, rem -= d
    "7: sec\n"
    "  lda __rc8\n sbc m65_rem\n sta m65_rem\n"
    "  lda __rc9\n sbc m65_rem+1\n sta m65_rem+1\n"
    "  lda __rc10\n sbc m65_rem+2\n sta m65_rem+2\n"
    "  lda __rc11\n sbc m65_rem+3\n sta m65_rem+3\n"
    "11: lda m65_rem+3\n cmp __rc7\n bne 12f\n"
    "  lda m65_rem+2\n cmp __rc6\n bne 12f\n"
    "  lda m65_rem+1\n cmp __rc5\n bne 12f\n"
    "  lda m65_rem\n cmp __rc4\n"
    "12: bcc 13f\n"                           // rem < d: done
    "  sec\n"
    "  lda m65_rem\n sbc __rc4\n sta m65_rem\n"
    "  lda m65_rem+1\n sbc __rc5\n sta m65_rem+1\n"
    "  lda m65_rem+2\n sbc __rc6\n sta m65_rem+2\n"
    "  lda m65_rem+3\n sbc __rc7\n sta m65_rem+3\n"
    "  inc __rc12\n bne 11b\n inc __rc13\n bne 11b\n inc __rc14\n bne 11b\n inc __rc15\n"
    "  bra 11b\n"
    "13: lda __rc14\n sta __rc2\n lda __rc15\n sta __rc3\n"
    "  lda __rc12\n ldx __rc13\n"
    "  rts\n"
    ".size m65_udivmod, . - m65_udivmod\n");

#define rem m65_rem

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
