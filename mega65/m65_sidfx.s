; The AdLib sound effects (item pickups, weapon select...), converted for a
; SID by tools/adlib2sid.py (SFX.DAT), played on the fourth SID's voice 1
; ($D460; the music has the other three SIDs). m65_sd.cpp starts an effect
; (the voice's patch, then these variables); m65_sidfx_tick, called from the
; timer interrupt 200 times a second, steps it at the original's 140Hz.
;
; An effect is a SID frequency per tick (16 bits, 0: key off), read in place
; in attic RAM. As on the AdLib, a note after a rest keys the voice on (the
; envelope starts), a note after a note only changes the pitch.
; In section .midtext: resident, in main memory (loaded with WOLF.DAT).

	.zeropage	m65_sidfx_ptr

	.section	.zp.bss,"aw",@nobits
	.globl	m65_sidfx_ptr
m65_sidfx_ptr:	.zero	4		; the next tick's frequency

	.section	.bss.m65_sidfx,"aw",@nobits
	.globl	m65_sidfx_left, m65_sidfx_acc, m65_sidfx_on, m65_sidfx_gate, m65_sidfx_wave
m65_sidfx_left:	.zero	2		; ticks still to play
m65_sidfx_acc:	.zero	1		; 140Hz from 200Hz: +140 a call, a tick at 200
m65_sidfx_on:	.zero	1
m65_sidfx_gate:	.zero	1		; keyed on?
m65_sidfx_wave:	.zero	1		; the control register's waveform bits

	.section	.midtext.m65_sidfx_tick,"ax",@progbits
	.globl	m65_sidfx_tick
m65_sidfx_tick:				; (from the interrupt: A, X, Y, Z are saved)
	lda	m65_sidfx_on
	beq	.Ldone
	lda	m65_sidfx_acc		; acc += 140; a tick when it reaches 200
	clc
	adc	#140
	bcs	1f			; (>= 256: acc - 200 is A + 56)
	cmp	#200
	bcc	2f
	sbc	#200			; (carry set)
	sta	m65_sidfx_acc
	bra	.Ltick
1:	adc	#55			; (+ carry: 56)
	sta	m65_sidfx_acc
	bra	.Ltick
2:	sta	m65_sidfx_acc
	rts
.Ltick:
	ldz	#1
	lda	[m65_sidfx_ptr],z
	tax				; X: frequency, high byte
	dez
	lda	[m65_sidfx_ptr],z	; A: low byte
	bne	.Lnote
	cpx	#0
	beq	.Lrest
.Lnote:
	sta	$d460
	stx	$d461
	lda	m65_sidfx_gate
	bne	.Lnext
	lda	m65_sidfx_wave		; after a rest: key on
	ora	#1
	sta	$d464
	lda	#1
	sta	m65_sidfx_gate
	bra	.Lnext
.Lrest:
	lda	m65_sidfx_gate
	beq	.Lnext
	lda	m65_sidfx_wave		; key off
	sta	$d464
	lda	#0
	sta	m65_sidfx_gate
.Lnext:
	clc				; ptr += 2
	lda	m65_sidfx_ptr
	adc	#2
	sta	m65_sidfx_ptr
	bcc	3f
	inc	m65_sidfx_ptr+1
	bne	3f
	inc	m65_sidfx_ptr+2
	bne	3f
	inc	m65_sidfx_ptr+3
3:	lda	m65_sidfx_left		; left--
	bne	4f
	dec	m65_sidfx_left+1
4:	dec	m65_sidfx_left
	lda	m65_sidfx_left
	ora	m65_sidfx_left+1
	bne	.Ldone
	lda	m65_sidfx_wave		; the end: key off (the release plays out)
	sta	$d464
	lda	#0
	sta	m65_sidfx_gate
	sta	m65_sidfx_on
.Ldone:
	rts
	.size	m65_sidfx_tick, . - m65_sidfx_tick
