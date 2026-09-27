; Music: the interrupt handler that plays MUSIC.DAT's songs on three SIDs
; ($D400, $D420, $D440) at 200 ticks per second (CIA 1 timer A; set up by
; m65_sd.cpp, which also starts and stops songs).
;
; A song is records of SID register writes (tools/midi2sid.py, sidpack.py):
;   count, count * (register offset from $D400, value), delay (16 bits:
;   ticks until the next record). A count of 0 ends the song: it starts
;   again. Songs are read in place in attic RAM through a 32-bit pointer.
;
; BRK (the B flag in the pushed status) goes on to m65_irq's report.
; Resident, since the interrupt may come while any overlay is mapped. It
; also streams the sound effects (m65_sfx_refill, m65_sfx.s).

	.zeropage	m65_mus_p, m65_mus_sp

	.section	.zp.bss,"aw",@nobits
	.globl	m65_mus_p
m65_mus_p:	.zero	4		; the next record
m65_mus_sp:	.zero	2		; (the stack pointer: 16 bits)

	.section	.bss.m65_music,"aw",@nobits
	.globl	m65_mus_start, m65_mus_wait, m65_mus_on
m65_mus_start:	.zero	4		; the song's first record
m65_mus_wait:	.zero	2		; ticks until the next record
m65_mus_on:	.zero	1		; playing?

	.section	.text.m65_music_irq,"ax",@progbits
	.globl	m65_music_irq
m65_music_irq:
	pha
	phx
	phy
	phz
	tsx				; the pushed status: 16-bit stack
	stx	m65_mus_sp
	tsy
	sty	m65_mus_sp+1
	ldy	#5
	lda	(m65_mus_sp),y
	and	#$10
	beq	1f
	plz				; a BRK: report it as before
	ply
	plx
	pla
	jmp	m65_irq
1:	lda	$dc0d			; acknowledge the CIA
	jsr	m65_sfx_refill		; sound effects (m65_sfx.s)
	jsr	m65_sidfx_tick		; the AdLib effects on the fourth SID (m65_sidfx.s)
	lda	m65_mus_on
	beq	.Ldone
	lda	m65_mus_wait
	ora	m65_mus_wait+1
	bne	.Lcount
.Lnext:
	ldz	#0
	lda	[m65_mus_p],z
	bne	2f
	ldx	#3			; the end: from the start again
3:	lda	m65_mus_start,x
	sta	m65_mus_p,x
	dex
	bpl	3b
	lda	[m65_mus_p],z		; (an empty song: stop)
	bne	2f
	sta	m65_mus_on
	bra	.Ldone
2:	tay				; Y: writes left
	inz
4:	lda	[m65_mus_p],z		; register
	tax
	inz
	lda	[m65_mus_p],z		; value
	inz
	sta	$d400,x
	dey
	bne	4b
	lda	[m65_mus_p],z		; delay
	sta	m65_mus_wait
	inz
	lda	[m65_mus_p],z
	sta	m65_mus_wait+1
	inz
	tza				; the pointer past the record
	clc
	adc	m65_mus_p
	sta	m65_mus_p
	bcc	5f
	inc	m65_mus_p+1
	bne	5f
	inc	m65_mus_p+2
	bne	5f
	inc	m65_mus_p+3
5:	lda	m65_mus_wait
	ora	m65_mus_wait+1
	beq	.Lnext			; no delay: the next record now
.Lcount:
	lda	m65_mus_wait		; one tick of the delay gone
	bne	6f
	dec	m65_mus_wait+1
6:	dec	m65_mus_wait
.Ldone:
	plz
	ply
	plx
	pla
	rti
