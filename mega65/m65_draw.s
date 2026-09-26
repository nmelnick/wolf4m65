; The renderer's column buffer (wl_draw.cpp): the sprite scaler builds a
; scaled texture column here and copies it to the screen columns with DMA.
; (The walls need none: the DMA controller scales them, see ScalePost.)

	.section	.bss.m65_colbuf,"aw",@nobits
	.globl	m65_colbuf
m65_colbuf:	.zero	200
