void f(void)
{
    __asm__ volatile(
        "lda #$00\n"
        "ldx #$00\n"
        "ldy #$00\n"
        "ldz #$00\n"
        "map\n"
        "eom\n"
        "ldz #$00\n" ::: "a", "x", "y");
}
