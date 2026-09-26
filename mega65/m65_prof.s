; Sampling profiler (make profile): an interrupt handler that counts where
; the CPU is, for m65_test.c to start and to report (see there).
;
; Every CIA 1 timer A interrupt adds one to a 16-bit counter in attic RAM
; (PROF_BASE) chosen by the interrupted PC, in 16-byte buckets:
;   PC outside the overlay window: counter PC >> 4          (bytes 0..8191)
;   PC in the window ($4000-$5FFF): counter 4096 + overlay * 512
;       + (PC - $4000) >> 4, the overlay being __ovl_cur    (bytes 8192..)
; BRK (the B flag in the pushed status) goes on to m65_irq's report.
; Resident: it may interrupt any overlay.

	.zeropage	__prof_p, __prof_sp, __prof_pcl

	.section	.zp.bss,"aw",@nobits
__prof_p:	.zero	4
__prof_sp:	.zero	2		; (the stack pointer: 16 bits)
__prof_pcl:	.zero	1

	.section	.midtext.m65_prof_irq,"ax",@progbits	; (resident, in main memory)
	.globl	m65_prof_irq
m65_prof_irq:
	pha
	phx
	phy
	phz
	tsx				; the pushed status: 16-bit stack
	stx	__prof_sp
	tsy
	sty	__prof_sp+1
	ldy	#5
	lda	(__prof_sp),y
	and	#$10
	beq	1f
	plz				; a BRK: report it as before
	ply
	plx
	pla
	jmp	m65_irq
1:	ldy	#6			; PC low
	lda	(__prof_sp),y
	sta	__prof_pcl
	ldy	#7			; PC high
	lda	(__prof_sp),y
	ldy	__prof_pcl
	cmp	#$40
	bcc	2f
	cmp	#$60
	bcs	2f
	sec				; in the window: PC - $4000
	sbc	#$40
	jsr	.Lshift
	lda	__ovl_cur		; + 8192 + overlay * 1024 (bytes)
	asl
	asl
	clc
	adc	#$20
	adc	__prof_p+1
	sta	__prof_p+1
	bra	3f
2:	jsr	.Lshift
3:	lda	#$7f			; PROF_BASE = $87F0000
	sta	__prof_p+2
	lda	#$08
	sta	__prof_p+3
	ldz	#0
	lda	[__prof_p],z
	clc
	adc	#1
	sta	[__prof_p],z
	bcc	4f
	inz
	lda	[__prof_p],z
	adc	#0
	sta	[__prof_p],z
4:	lda	$dc0d			; acknowledge the CIA
	plz
	ply
	plx
	pla
	rti

; A:Y (high:low) >> 3, even -> __prof_p (the byte offset of the counter).
.Lshift:
	sta	__prof_p+1
	tya
	lsr	__prof_p+1
	ror
	lsr	__prof_p+1
	ror
	lsr	__prof_p+1
	ror
	and	#$fe
	sta	__prof_p
	rts
