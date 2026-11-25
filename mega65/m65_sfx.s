; Sound effects: streaming the game's digitized sounds from attic RAM to the
; four audio DMA channels (m65_sd.cpp starts and stops them).
;
; Audio DMA reads chip RAM only, and the sounds (8-bit unsigned, ~7kHz) are
; in the VSWAP file in attic RAM. So each channel loops over a 512-byte ring
; in chip RAM (M65_SFX_RING + channel * 2048: 290ms), and m65_sfx_refill,
; called from the music player's timer interrupt 200 times a second, copies
; from attic what the channel has played since (about 35 bytes a tick; the
; ring is long, as Xemu advances the channels in chunks and interrupts can
; come late). After a
; sound's last byte it writes silence ($80) until that has been played too,
; then stops the channel.
;
; Per channel (the m65_sfx_* arrays, set up by m65_sd.cpp before starting a
; channel): state (0 idle, 1 playing), source (32-bit, the next byte in
; attic), left (bytes of the sound still to copy), tail (bytes of silence
; still to play after that), wp (where in the ring the next byte goes).

M65_SFX_RING = $10000			; 4 x 2048 bytes, $10000-$11FFF (bank 1)

	.zeropage	sfx_src, sfx_dst

	.section	.zp.bss,"aw",@nobits
sfx_src:	.zero	4		; (the current channel's source pointer)
sfx_dst:	.zero	4		; (its ring pointer)

	.section	.bss.m65_sfx,"aw",@nobits
	.globl	m65_sfx_state, m65_sfx_src, m65_sfx_left, m65_sfx_tail, m65_sfx_wp
m65_sfx_state:	.zero	4
m65_sfx_src:	.zero	16		; 4 x 32 bits
m65_sfx_left:	.zero	8		; 4 x 16 bits
m65_sfx_tail:	.zero	8		; 4 x 16 bits
m65_sfx_wp:	.zero	8		; 4 x 16 bits (0..2047)
sfx_ch:		.zero	1		; (the channel being refilled)
sfx_n:		.zero	2		; (bytes to write this tick)
sfx_k:		.zero	1		; (bytes in this run)

	.section	.text.m65_sfx_refill,"ax",@progbits
	.globl	m65_sfx_refill
m65_sfx_refill:				; (from the interrupt: A, X, Y, Z are saved)
	ldx	#3
.Lchannel:
	lda	m65_sfx_state,x
	bne	1f
	jmp	.Lnext
1:	stx	sfx_ch
	txa				; X *= 2 (16-bit arrays), *16 (registers)
	asl
	tay				; Y: 16-bit array index
	txa
	asl
	asl
	asl
	asl
	tax				; X: register offset from $D720
	; play offset po = (current address - ring) & 2047; read until stable
2:	lda	$d72b,x
	sta	sfx_n+1
	lda	$d72a,x
	sta	sfx_n
	lda	$d72b,x
	cmp	sfx_n+1
	bne	2b
	; n = (po - wp) & 2047: the bytes played since the last refill
	sec
	lda	sfx_n
	sbc	m65_sfx_wp,y
	sta	sfx_n
	lda	sfx_n+1
	sbc	m65_sfx_wp+1,y
	and	#7			; (& 2047: the ring base is 2048-aligned)
	sta	sfx_n+1
	; the ring pointer: M65_SFX_RING + channel * 2048 + wp
	lda	m65_sfx_wp,y
	sta	sfx_dst
	lda	sfx_ch
	asl
	asl
	asl
	clc
	adc	#>M65_SFX_RING
	adc	m65_sfx_wp+1,y
	sta	sfx_dst+1
	lda	#^M65_SFX_RING		; (bank $01)
	sta	sfx_dst+2
	lda	#0
	sta	sfx_dst+3
	; the source pointer
	ldx	sfx_ch
	txa
	asl
	asl
	tax
	lda	m65_sfx_src,x
	sta	sfx_src
	lda	m65_sfx_src+1,x
	sta	sfx_src+1
	lda	m65_sfx_src+2,x
	sta	sfx_src+2
	lda	m65_sfx_src+3,x
	sta	sfx_src+3
.Lrun:	; runs: up to the ring's end, up to 255 bytes, up to what is left
	lda	sfx_n
	ora	sfx_n+1
	bne	3f
	jmp	.Ldone
3:	; k = min(n, 255, 2048 - wp)
	lda	sfx_n+1
	beq	4f
	lda	#255
	bra	5f
4:	lda	sfx_n
5:	sta	sfx_k
	sec				; 2048 - wp
	lda	#0
	sbc	m65_sfx_wp,y
	sta	sfx_src+0		; (scratch, restored below)
	lda	#8
	sbc	m65_sfx_wp+1,y
	bne	6f			; >= 256: no limit from the ring's end
	lda	sfx_src+0
	cmp	sfx_k
	bcs	6f
	sta	sfx_k
6:	ldx	sfx_ch			; (restore sfx_src+0)
	txa
	asl
	asl
	tax
	lda	m65_sfx_src,x
	sta	sfx_src
	; the sound's bytes, or silence once it has none left
	lda	m65_sfx_left,y
	ora	m65_sfx_left+1,y
	beq	.Lsilence
	lda	m65_sfx_left+1,y	; k = min(k, left)
	bne	7f
	lda	m65_sfx_left,y
	cmp	sfx_k
	bcs	7f
	sta	sfx_k
7:	ldx	sfx_k
	ldz	#0
8:	lda	[sfx_src],z
	sta	[sfx_dst],z
	inz
	dex
	bne	8b
	; source += k; left -= k
	ldx	sfx_ch
	txa
	asl
	asl
	tax
	clc
	lda	sfx_src
	adc	sfx_k
	sta	sfx_src
	sta	m65_sfx_src,x
	lda	sfx_src+1
	adc	#0
	sta	sfx_src+1
	sta	m65_sfx_src+1,x
	lda	sfx_src+2
	adc	#0
	sta	sfx_src+2
	sta	m65_sfx_src+2,x
	lda	sfx_src+3
	adc	#0
	sta	sfx_src+3
	sta	m65_sfx_src+3,x
	sec
	lda	m65_sfx_left,y
	sbc	sfx_k
	sta	m65_sfx_left,y
	lda	m65_sfx_left+1,y
	sbc	#0
	sta	m65_sfx_left+1,y
	bra	.Lwrote
.Lsilence:
	ldx	sfx_k
	ldz	#0
	lda	#$80
9:	sta	[sfx_dst],z
	inz
	dex
	bne	9b
	sec				; tail -= k; played out: stop the channel
	lda	m65_sfx_tail,y
	sbc	sfx_k
	sta	m65_sfx_tail,y
	lda	m65_sfx_tail+1,y
	sbc	#0
	sta	m65_sfx_tail+1,y
	bcs	.Lwrote
	ldx	sfx_ch
	lda	#0
	sta	m65_sfx_state,x
	txa
	asl
	asl
	asl
	asl
	tax
	lda	#0
	sta	$d720,x			; (channel off)
	bra	.Ldone
.Lwrote:	; wp = (wp + k) & 2047; n -= k; the ring pointer follows
	clc
	lda	m65_sfx_wp,y
	adc	sfx_k
	sta	m65_sfx_wp,y
	sta	sfx_dst
	lda	m65_sfx_wp+1,y
	adc	#0
	and	#7
	sta	m65_sfx_wp+1,y
	sta	sfx_src+0		; (scratch)
	lda	sfx_ch
	asl
	asl
	asl
	clc
	adc	#>M65_SFX_RING
	adc	sfx_src+0
	sta	sfx_dst+1
	ldx	sfx_ch			; (restore sfx_src+0)
	txa
	asl
	asl
	tax
	lda	m65_sfx_src,x
	sta	sfx_src
	sec
	lda	sfx_n
	sbc	sfx_k
	sta	sfx_n
	lda	sfx_n+1
	sbc	#0
	sta	sfx_n+1
	jmp	.Lrun
.Ldone:
	ldx	sfx_ch
.Lnext:
	dex
	bmi	1f
	jmp	.Lchannel
1:	ldz	#0
	rts
	.size	m65_sfx_refill, . - m65_sfx_refill
