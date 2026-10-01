; The ray caster's per-column work (AsmRefresh in wl_draw.cpp), in assembly.
;
; m65_raysetup starts the ray of screen column pixx, as the top of the
; original's loop does: the ray's angle and quadrant, its steps from
; finetangent, the first intercepts (two FixedMuls, on the math unit) and
; tiles. It sets xtilestep, ytilestep, xtile, ytile, xintercept, yintercept,
; texdelta (0) and the ray's steps, m65_rxstep and m65_rystep (the
; original's xstep, ystep).
;
; m65_trace steps the ray through the map, exactly as the original's two
; loops do (the vertical-crossing loop and the horizontal-crossing loop,
; which hand over to each other), marking the tiles it crosses in spotvis,
; until something needs the C code:
;   1  a tile at xspot (vertical crossing): tilehit set
;   2  a tile at yspot (horizontal crossing): tilehit set
;   3  the map's edge on a vertical crossing   (the original: HitHorizBorder)
;   4  the map's edge on a horizontal crossing (the original: HitVertBorder)
;   (the original's "spot beyond the map" cannot happen: a spot is only used
;   inside the map)
; entry: 0 starts a ray (the vertical loop's top), 1 goes on after a tile the
; ray passes (the original's passvert), 2 the same for passhoriz.
;
; The ray's variables are in the zero page (wl_draw.cpp defines the game's;
; xspot and yspot are not kept, nothing else uses them). TraceSetup sets,
; once at start-up: m65_pa_base and m65_ft_base (pixelangle's and finetangent's
; far addresses), m65_trp+2/+3 (the bank of tilemap and spotvis: chip RAM,
; both 256-byte aligned and in the same bank), m65_tmhi (tilemap's address,
; bits 8-15) and m65_svd (spotvis's minus tilemap's, in pages). The map is
; 64 x 64: a tile's address is base + x * 64 + y, here a pointer to the x's
; row and y in Z.
;
; The compiler's code needs Z = 0: set again before returning.

	.zeropage	m65_trp
	.zeropage	m65_rxstep
	.zeropage	m65_rystep
	.zeropage	m65_tmhi
	.zeropage	m65_svd
	.zeropage	m65_trz
	.zeropage	xtile
	.zeropage	ytile
	.zeropage	xtilestep
	.zeropage	ytilestep
	.zeropage	xintercept
	.zeropage	yintercept

	.section	.zp.bss,"aw",@nobits
	.globl	m65_trp
m65_trp:	.zero	4		; the 32-bit pointer into the map
	.globl	m65_rxstep
m65_rxstep:	.zero	4		; (m65_rystep must follow)
	.globl	m65_rystep
m65_rystep:	.zero	4
	.globl	m65_tmhi
m65_tmhi:	.zero	1
	.globl	m65_svd
m65_svd:	.zero	1
m65_trz:	.zero	1		; Z at the tile handed to the C code

	.section	.bss.m65_ray_bases,"aw",@nobits
	.globl	m65_pa_base
m65_pa_base:	.zero	4
	.globl	m65_ft_base
m65_ft_base:	.zero	4

; ---------------------------------------------------------------------------
	.section	.text.m65_raysetup,"ax",@progbits
	.globl	m65_raysetup
	.type	m65_raysetup,@function
m65_raysetup:
	; angl = midangle + pixelangle[pixx], wrapped to 0..3599
	lda	pixx
	asl
	sta	__rc4
	lda	pixx+1
	rol
	sta	__rc5
	clc
	lda	__rc4
	adc	m65_pa_base
	sta	__rc4
	lda	__rc5
	adc	m65_pa_base+1
	sta	__rc5
	lda	m65_pa_base+2
	adc	#0
	sta	__rc6
	lda	m65_pa_base+3
	sta	__rc7
	ldz	#0
	clc
	lda	[__rc4],z
	adc	midangle
	sta	__rc2
	inz
	lda	[__rc4],z
	adc	midangle+1
	sta	__rc3
	bpl	1f
	clc
	lda	__rc2
	adc	#<3600
	sta	__rc2
	lda	__rc3
	adc	#>3600
	sta	__rc3
1:	lda	__rc2
	cmp	#<3600
	lda	__rc3
	sbc	#>3600
	bvc	2f
	eor	#$80
2:	bmi	3f
	sec
	lda	__rc2
	sbc	#<3600
	sta	__rc2
	lda	__rc3
	sbc	#>3600
	sta	__rc3
3:
	; the quadrant in X, the angle within it (0..899) in __rc2/3
	ldx	#0
4:	lda	__rc2
	cmp	#<900
	lda	__rc3
	sbc	#>900
	bcc	5f
	lda	__rc2			; (carry set)
	sbc	#<900
	sta	__rc2
	lda	__rc3
	sbc	#>900
	sta	__rc3
	inx
	bra	4b
5:	stx	__rc14
	; finetangent's index for xstep in __rc2/3, for ystep in __rc8/9: the
	; angle and 899 - angle in the odd quadrants, the other way in the even
	sec
	lda	#<899
	sbc	__rc2
	sta	__rc8
	lda	#>899
	sbc	__rc3
	sta	__rc9
	txa
	lsr
	bcs	6f
	lda	__rc2
	ldy	__rc8
	sta	__rc8
	sty	__rc2
	lda	__rc3
	ldy	__rc9
	sta	__rc9
	sty	__rc3
6:
	; xtilestep, ytilestep: 1 or -1; xtile, ytile
	lda	.Lxneg,x
	beq	7f
	lda	#$ff
	sta	xtilestep
	sta	xtilestep+1
	bra	8f
7:	sta	xtilestep+1
	inc
	sta	xtilestep
8:	lda	.Lyneg,x
	beq	9f
	lda	#$ff
	sta	ytilestep
	sta	ytilestep+1
	bra	10f
9:	sta	ytilestep+1
	inc
	sta	ytilestep
10:	clc
	lda	focaltx
	adc	xtilestep
	sta	xtile
	lda	focaltx+1
	adc	xtilestep+1
	sta	xtile+1
	clc
	lda	focalty
	adc	ytilestep
	sta	ytile
	lda	focalty+1
	adc	ytilestep+1
	sta	ytile+1
	lda	#0
	sta	texdelta
	sta	texdelta+1
	sta	$d777			; (the partials are at most $10000)

	; xstep = +-finetangent[..]; xintercept = FixedMul(xstep, ypartial) + viewx
	; with ypartial = ypartialup going down the map (ytilestep 1), else
	; ypartialdown
	lda	__rc2
	ldy	__rc3
	ldx	#0
	jsr	.Lftread
	ldx	__rc14
	lda	.Lyneg,x
	bne	11f
	lda	ypartialup
	sta	$d774
	lda	ypartialup+1
	sta	$d775
	lda	ypartialup+2
	bra	12f
11:	lda	ypartialdown
	sta	$d774
	lda	ypartialdown+1
	sta	$d775
	lda	ypartialdown+2
12:	sta	$d776
	ldy	.Lxneg,x
	ldx	#0
	jsr	.Lfixedmul
	clc
	lda	__rc10
	adc	viewx
	sta	xintercept
	lda	__rc11
	adc	viewx+1
	sta	xintercept+1
	lda	__rc12
	adc	viewx+2
	sta	xintercept+2
	lda	__rc13
	adc	viewx+3
	sta	xintercept+3

	; ystep = +-finetangent[..]; yintercept = FixedMul(ystep, xpartial) + viewy
	lda	__rc8
	ldy	__rc9
	ldx	#4
	jsr	.Lftread
	ldx	__rc14
	lda	.Lxneg,x
	bne	13f
	lda	xpartialup
	sta	$d774
	lda	xpartialup+1
	sta	$d775
	lda	xpartialup+2
	bra	14f
13:	lda	xpartialdown
	sta	$d774
	lda	xpartialdown+1
	sta	$d775
	lda	xpartialdown+2
14:	sta	$d776
	ldy	.Lyneg,x
	ldx	#4
	jsr	.Lfixedmul
	clc
	lda	__rc10
	adc	viewy
	sta	yintercept
	lda	__rc11
	adc	viewy+1
	sta	yintercept+1
	lda	__rc12
	adc	viewy+2
	sta	yintercept+2
	lda	__rc13
	adc	viewy+3
	sta	yintercept+3
	ldz	#0
	rts

; A, Y = an index into finetangent; X = 0 for xstep, 4 for ystep: the entry
; (never negative) to the step and to the multiplier's first factor.
.Lftread:
	sty	__rc5
	asl
	rol	__rc5
	asl
	rol	__rc5
	clc
	adc	m65_ft_base
	sta	__rc4
	lda	__rc5
	adc	m65_ft_base+1
	sta	__rc5
	lda	m65_ft_base+2
	adc	#0
	sta	__rc6
	lda	m65_ft_base+3
	sta	__rc7
	ldz	#0
	lda	[__rc4],z
	sta	$d770
	sta	m65_rxstep,x
	inz
	lda	[__rc4],z
	sta	$d771
	sta	m65_rxstep+1,x
	inz
	lda	[__rc4],z
	sta	$d772
	sta	m65_rxstep+2,x
	inz
	lda	[__rc4],z
	sta	$d773
	sta	m65_rxstep+3,x
	rts

; Wolf3D's FixedMul of the step at X (its size is in the multiplier, with
; the other factor) when Y is 0, of minus the step otherwise, which is then
; also what the step becomes: the product to __rc10-13. As m65_fixedmul:
; (p + $8000) >> 16, which for a negative p = -|p| is -((|p| + $7FFF) >> 16).
.Lfixedmul:
	cpy	#0
	beq	1f
	clc
	lda	$d778
	adc	#$ff
	lda	$d779
	adc	#$7f
	bra	2f
1:	lda	$d779
	asl
2:	lda	$d77a
	adc	#0
	sta	__rc10
	lda	$d77b
	adc	#0
	sta	__rc11
	lda	$d77c
	adc	#0
	sta	__rc12
	lda	$d77d
	adc	#0
	sta	__rc13
	cpy	#0
	beq	3f
	sec
	lda	#0
	sbc	__rc10
	sta	__rc10
	lda	#0
	sbc	__rc11
	sta	__rc11
	lda	#0
	sbc	__rc12
	sta	__rc12
	lda	#0
	sbc	__rc13
	sta	__rc13
	sec
	lda	#0
	sbc	m65_rxstep,x
	sta	m65_rxstep,x
	lda	#0
	sbc	m65_rxstep+1,x
	sta	m65_rxstep+1,x
	lda	#0
	sbc	m65_rxstep+2,x
	sta	m65_rxstep+2,x
	lda	#0
	sbc	m65_rxstep+3,x
	sta	m65_rxstep+3,x
3:	rts

; By quadrant: is the ray going left (xtilestep -1), up (ytilestep -1)?
.Lxneg:	.byte	0, 1, 1, 0
.Lyneg:	.byte	1, 1, 0, 0
	.size	m65_raysetup, . - m65_raysetup

; ---------------------------------------------------------------------------
	.section	.text.m65_trace,"ax",@progbits
	.globl	m65_trace
	.type	m65_trace,@function
m65_trace:
	cmp	#1
	beq	.Lrevert
	cmp	#2
	bne	.Lvtop
	ldz	m65_trz
	jmp	.Lpasshoriz
.Lrevert:
	ldz	m65_trz
	bra	.Lpassvert

	; ---- the vertical-crossing loop ----
.Lvtop:
	; if (ytilestep == -1 && (yintercept >> 16) <= ytile) goto horizentry;
	; if (ytilestep ==  1 && (yintercept >> 16) >= ytile) goto horizentry;
	lda	ytilestep+1
	bmi	1f
	lda	yintercept+2
	cmp	ytile
	lda	yintercept+3
	sbc	ytile+1
	bvc	2f
	eor	#$80
2:	bpl	.Lhentry
	bra	.Lventry
1:	lda	ytile
	cmp	yintercept+2
	lda	ytile+1
	sbc	yintercept+3
	bvc	3f
	eor	#$80
3:	bpl	.Lhentry
.Lventry:
	; the edge: (uint32)yintercept > 64 * 65536 - 1 || (word)xtile >= 64
	lda	yintercept+3
	bne	.Lvedge
	ldz	yintercept+2
	cpz	#64
	bcs	.Lvedge
	lda	xtile+1
	bne	.Lvedge
	ldx	xtile
	cpx	#64
	bcs	.Lvedge
	; tilehit = tilemap[xtile][yintercept >> 16]
	lda	.Lrowlo,x
	sta	m65_trp
	lda	.Lrowhi,x
	adc	m65_tmhi		; (carry clear)
	sta	m65_trp+1
	lda	[m65_trp],z
	beq	.Lpassvert
	sta	tilehit
	stz	m65_trz
	ldz	#0
	stz	tilehit+1
	lda	#1
	rts
.Lpassvert:
	; spotvis[xtile][yintercept >> 16] = 1
	clc
	lda	m65_trp+1
	adc	m65_svd
	sta	m65_trp+1
	lda	#1
	sta	[m65_trp],z
	; xtile += xtilestep; yintercept += ystep
	clc
	lda	xtile
	adc	xtilestep
	sta	xtile
	lda	xtile+1
	adc	xtilestep+1
	sta	xtile+1
	clc
	lda	yintercept
	adc	m65_rystep
	sta	yintercept
	lda	yintercept+1
	adc	m65_rystep+1
	sta	yintercept+1
	lda	yintercept+2
	adc	m65_rystep+2
	sta	yintercept+2
	lda	yintercept+3
	adc	m65_rystep+3
	sta	yintercept+3
	jmp	.Lvtop

.Lvedge:
	ldz	#0
	lda	#3
	rts
.Lhedge:
	ldz	#0
	lda	#4
	rts

	; ---- the horizontal-crossing loop ----
.Lhtop:
	; if (xtilestep == -1 && (xintercept >> 16) <= xtile) goto vertentry;
	; if (xtilestep ==  1 && (xintercept >> 16) >= xtile) goto vertentry;
	lda	xtilestep+1
	bmi	1f
	lda	xintercept+2
	cmp	xtile
	lda	xintercept+3
	sbc	xtile+1
	bvc	2f
	eor	#$80
2:	bmi	.Lhentry
	jmp	.Lventry
1:	lda	xtile
	cmp	xintercept+2
	lda	xtile+1
	sbc	xintercept+3
	bvc	3f
	eor	#$80
3:	bmi	.Lhentry
	jmp	.Lventry
.Lhentry:
	; the edge: (uint32)xintercept > 64 * 65536 - 1 || (word)ytile >= 64
	lda	xintercept+3
	bne	.Lhedge
	ldx	xintercept+2
	cpx	#64
	bcs	.Lhedge
	lda	ytile+1
	bne	.Lhedge
	ldz	ytile
	cpz	#64
	bcs	.Lhedge
	; tilehit = tilemap[xintercept >> 16][ytile]
	lda	.Lrowlo,x
	sta	m65_trp
	lda	.Lrowhi,x
	adc	m65_tmhi		; (carry clear)
	sta	m65_trp+1
	lda	[m65_trp],z
	beq	.Lpasshoriz
	sta	tilehit
	stz	m65_trz
	ldz	#0
	stz	tilehit+1
	lda	#2
	rts
.Lpasshoriz:
	; spotvis[xintercept >> 16][ytile] = 1
	clc
	lda	m65_trp+1
	adc	m65_svd
	sta	m65_trp+1
	lda	#1
	sta	[m65_trp],z
	; ytile += ytilestep; xintercept += xstep
	clc
	lda	ytile
	adc	ytilestep
	sta	ytile
	lda	ytile+1
	adc	ytilestep+1
	sta	ytile+1
	clc
	lda	xintercept
	adc	m65_rxstep
	sta	xintercept
	lda	xintercept+1
	adc	m65_rxstep+1
	sta	xintercept+1
	lda	xintercept+2
	adc	m65_rxstep+2
	sta	xintercept+2
	lda	xintercept+3
	adc	m65_rxstep+3
	sta	xintercept+3
	jmp	.Lhtop

; A map row's offset, x * 64: its low byte and its pages.
.Lrowlo:
	.byte	0,64,128,192, 0,64,128,192, 0,64,128,192, 0,64,128,192
	.byte	0,64,128,192, 0,64,128,192, 0,64,128,192, 0,64,128,192
	.byte	0,64,128,192, 0,64,128,192, 0,64,128,192, 0,64,128,192
	.byte	0,64,128,192, 0,64,128,192, 0,64,128,192, 0,64,128,192
.Lrowhi:
	.byte	0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3
	.byte	4,4,4,4, 5,5,5,5, 6,6,6,6, 7,7,7,7
	.byte	8,8,8,8, 9,9,9,9, 10,10,10,10, 11,11,11,11
	.byte	12,12,12,12, 13,13,13,13, 14,14,14,14, 15,15,15,15
	.size	m65_trace, . - m65_trace
