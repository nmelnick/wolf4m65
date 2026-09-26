; The inner loop of ScalePost (wl_draw.cpp): scale one texture column into
; a column buffer, exactly as the original C loop does:
;
;     col = tex[yw]; y = yend;
;     while (ytop <= y) {
;         colbuf[y] = col;
;         ywcount -= TEXTURESIZE / 2;
;         if (ywcount <= 0) {
;             do { ywcount += yd; yw--; } while (ywcount <= 0);
;             if (yw < 0) break;              // (row y was drawn)
;             col = tex[yw];
;         }
;         y--;
;     }
;
; The texture is read in place (far memory, 32-bit pointer) instead of
; being copied near first. Inputs in the m65_sc_* variables (the caller
; checks ytop <= yend); returns in A the last row drawn (rows A..yend of
; m65_colbuf are then the column). Resident, to be called cheaply from the
; renderer's overlay. Uses the Z register (yw), and leaves it 0 as compiled
; code expects ((zp) addressing is (zp),z on the 45GS02).

	.zeropage	m65_sc_tex, m65_sc_cnt, m65_sc_yd, m65_sc_ytop

	.section	.zp.bss,"aw",@nobits
	.globl	m65_sc_tex, m65_sc_cnt, m65_sc_yd, m65_sc_ytop
m65_sc_tex:	.zero	4		; far address of the texture column
m65_sc_cnt:	.zero	2		; ywcount
m65_sc_yd:	.zero	2		; yd
m65_sc_ytop:	.zero	1		; ytop

	.section	.bss.m65_sc_more,"aw",@nobits
	.globl	m65_sc_yend, m65_sc_yw
m65_sc_yend:	.zero	1		; yend
m65_sc_yw:	.zero	1		; yw (0..63)

	.section	.bss.m65_colbuf,"aw",@nobits
	.globl	m65_colbuf
m65_colbuf:	.zero	200

	.section	.text.m65_scalecol,"ax",@progbits
	.globl	m65_scalecol
	.type	m65_scalecol,@function
m65_scalecol:
	ldz	m65_sc_yw
	lda	[m65_sc_tex],z
	tax				; X: the colour
	ldy	m65_sc_yend		; Y: the row
1:	txa
	sta	m65_colbuf,y
	sec				; ywcount -= 32
	lda	m65_sc_cnt
	sbc	#32
	sta	m65_sc_cnt
	lda	m65_sc_cnt+1
	sbc	#0
	sta	m65_sc_cnt+1
	bmi	2f			; <= 0: next texel(s)
	ora	m65_sc_cnt
	bne	4f
2:	dez				; yw--; ywcount += yd
	clc
	lda	m65_sc_cnt
	adc	m65_sc_yd
	sta	m65_sc_cnt
	lda	m65_sc_cnt+1
	adc	m65_sc_yd+1
	sta	m65_sc_cnt+1
	bmi	2b			; while (ywcount <= 0)
	ora	m65_sc_cnt
	beq	2b
	tza				; yw < 0: done
	bmi	5f
	lda	[m65_sc_tex],z
	tax
4:	cpy	m65_sc_ytop		; while (ytop <= --y)
	beq	5f
	dey
	bra	1b
5:	ldz	#0			; (compiled code expects Z = 0)
	tya
	rts
	.size	m65_scalecol, . - m65_scalecol
