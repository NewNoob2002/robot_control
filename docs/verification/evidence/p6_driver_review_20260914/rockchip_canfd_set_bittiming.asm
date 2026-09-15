
docs/verification/evidence/p6_driver_review_20260914/target_code/rockchip_canfd_set_bittiming.bin:     file format binary


Disassembly of section .data:

ffff8000089ed87c <.data>:
ffff8000089ed87c:	aa0003e7 	mov	x7, x0
ffff8000089ed880:	b94a4c00 	ldr	w0, [x0, #2636]
ffff8000089ed884:	b94a44e1 	ldr	w1, [x7, #2628]
ffff8000089ed888:	53017c00 	lsr	w0, w0, #1
ffff8000089ed88c:	51000400 	sub	w0, w0, #0x1
ffff8000089ed890:	b94a40e3 	ldr	w3, [x7, #2624]
ffff8000089ed894:	51000421 	sub	w1, w1, #0x1
ffff8000089ed898:	b94a48e2 	ldr	w2, [x7, #2632]
ffff8000089ed89c:	53183c21 	ubfiz	w1, w1, #8, #16
ffff8000089ed8a0:	51000442 	sub	w2, w2, #0x1
ffff8000089ed8a4:	2a004020 	orr	w0, w1, w0, lsl #16
ffff8000089ed8a8:	b94a3ce1 	ldr	w1, [x7, #2620]
ffff8000089ed8ac:	0b030021 	add	w1, w1, w3
ffff8000089ed8b0:	51000421 	sub	w1, w1, #0x1
ffff8000089ed8b4:	12003c21 	and	w1, w1, #0xffff
ffff8000089ed8b8:	2a026021 	orr	w1, w1, w2, lsl #24
ffff8000089ed8bc:	2a010000 	orr	w0, w0, w1
ffff8000089ed8c0:	912800e1 	add	x1, x7, #0xa00
ffff8000089ed8c4:	b940d422 	ldr	w2, [x1, #212]
ffff8000089ed8c8:	36100042 	tbz	w2, #2, 0xffff8000089ed8d0
ffff8000089ed8cc:	32010000 	orr	w0, w0, #0x80000000
ffff8000089ed8d0:	f9419022 	ldr	x2, [x1, #800]
ffff8000089ed8d4:	91040042 	add	x2, x2, #0x100
ffff8000089ed8d8:	d50332bf 	dmb	oshst
ffff8000089ed8dc:	b9000040 	str	w0, [x2]
ffff8000089ed8e0:	b940d420 	ldr	w0, [x1, #212]
ffff8000089ed8e4:	36280580 	tbz	w0, #5, 0xffff8000089ed994
ffff8000089ed8e8:	912990e2 	add	x2, x7, #0xa64
ffff8000089ed8ec:	b94a50e6 	ldr	w6, [x7, #2640]
ffff8000089ed8f0:	297f1444 	ldp	w4, w5, [x2, #-8]
ffff8000089ed8f4:	29400043 	ldp	w3, w0, [x2]
ffff8000089ed8f8:	0b050084 	add	w4, w4, w5
ffff8000089ed8fc:	b94a6ce2 	ldr	w2, [x7, #2668]
ffff8000089ed900:	51000484 	sub	w4, w4, #0x1
ffff8000089ed904:	51000463 	sub	w3, w3, #0x1
ffff8000089ed908:	51000400 	sub	w0, w0, #0x1
ffff8000089ed90c:	12003c65 	and	w5, w3, #0xffff
ffff8000089ed910:	52923803 	mov	w3, #0x91c0                	// #37312
ffff8000089ed914:	53017c42 	lsr	w2, w2, #1
ffff8000089ed918:	12003c00 	and	w0, w0, #0xffff
ffff8000089ed91c:	51000442 	sub	w2, w2, #0x1
ffff8000089ed920:	12003c84 	and	w4, w4, #0xffff
ffff8000089ed924:	12003c42 	and	w2, w2, #0xffff
ffff8000089ed928:	72a00423 	movk	w3, #0x21, lsl #16
ffff8000089ed92c:	6b0300df 	cmp	w6, w3
ffff8000089ed930:	540001c9 	b.ls	0xffff8000089ed968  // b.plast
ffff8000089ed934:	b940a023 	ldr	w3, [x1, #160]
ffff8000089ed938:	1ac60863 	udiv	w3, w3, w6
ffff8000089ed93c:	531f7863 	lsl	w3, w3, #1
ffff8000089ed940:	7102fc7f 	cmp	w3, #0xbf
ffff8000089ed944:	540007e8 	b.hi	0xffff8000089eda40  // b.pmore
ffff8000089ed948:	52800066 	mov	w6, #0x3                   	// #3
ffff8000089ed94c:	1ac60863 	udiv	w3, w3, w6
ffff8000089ed950:	f9419026 	ldr	x6, [x1, #800]
ffff8000089ed954:	531f7863 	lsl	w3, w3, #1
ffff8000089ed958:	32000063 	orr	w3, w3, #0x1
ffff8000089ed95c:	910420c6 	add	x6, x6, #0x108
ffff8000089ed960:	d50332bf 	dmb	oshst
ffff8000089ed964:	b90000c3 	str	w3, [x6]
ffff8000089ed968:	531b68a3 	lsl	w3, w5, #5
ffff8000089ed96c:	2a004480 	orr	w0, w4, w0, lsl #17
ffff8000089ed970:	2a022462 	orr	w2, w3, w2, lsl #9
ffff8000089ed974:	2a000042 	orr	w2, w2, w0
ffff8000089ed978:	b940d420 	ldr	w0, [x1, #212]
ffff8000089ed97c:	36100040 	tbz	w0, #2, 0xffff8000089ed984
ffff8000089ed980:	320b0042 	orr	w2, w2, #0x200000
ffff8000089ed984:	f9419020 	ldr	x0, [x1, #800]
ffff8000089ed988:	91041000 	add	x0, x0, #0x104
ffff8000089ed98c:	d50332bf 	dmb	oshst
ffff8000089ed990:	b9000002 	str	w2, [x0]
ffff8000089ed994:	b94a30e0 	ldr	w0, [x7, #2608]
ffff8000089ed998:	5281a802 	mov	w2, #0xd40                 	// #3392
ffff8000089ed99c:	72a00062 	movk	w2, #0x3, lsl #16
ffff8000089ed9a0:	6b02001f 	cmp	w0, w2
ffff8000089ed9a4:	54000528 	b.hi	0xffff8000089eda48  // b.pmore
ffff8000089ed9a8:	52986a02 	mov	w2, #0xc350                	// #50000
ffff8000089ed9ac:	6b02001f 	cmp	w0, w2
ffff8000089ed9b0:	52800280 	mov	w0, #0x14                  	// #20
ffff8000089ed9b4:	528000a2 	mov	w2, #0x5                   	// #5
ffff8000089ed9b8:	1a829000 	csel	w0, w0, w2, ls	// ls = plast
ffff8000089ed9bc:	9000e7a8 	adrp	x8, 0xffff80000a6e1000
ffff8000089ed9c0:	9108c108 	add	x8, x8, #0x230
ffff8000089ed9c4:	b903b020 	str	w0, [x1, #944]
ffff8000089ed9c8:	9101e108 	add	x8, x8, #0x78
ffff8000089ed9cc:	39408d00 	ldrb	w0, [x8, #35]
ffff8000089ed9d0:	36000400 	tbz	w0, #0, 0xffff8000089eda50
ffff8000089ed9d4:	d503233f 	paciasp
ffff8000089ed9d8:	a9bf7bfd 	stp	x29, x30, [sp, #-16]!
ffff8000089ed9dc:	52802001 	mov	w1, #0x100                 	// #256
ffff8000089ed9e0:	910003fd 	mov	x29, sp
ffff8000089ed9e4:	f94690e0 	ldr	x0, [x7, #3360]
ffff8000089ed9e8:	b00068e3 	adrp	x3, 0xffff80000970a000
ffff8000089ed9ec:	9105a063 	add	x3, x3, #0x168
ffff8000089ed9f0:	b0009622 	adrp	x2, 0xffff800009cb2000
ffff8000089ed9f4:	91008c63 	add	x3, x3, #0x23
ffff8000089ed9f8:	91387042 	add	x2, x2, #0xe1c
ffff8000089ed9fc:	97fffe6e 	bl	0xffff8000089ed3b4
ffff8000089eda00:	2a0003e4 	mov	w4, w0
ffff8000089eda04:	f94690e0 	ldr	x0, [x7, #3360]
ffff8000089eda08:	52802081 	mov	w1, #0x104                 	// #260
ffff8000089eda0c:	97fffe6a 	bl	0xffff8000089ed3b4
ffff8000089eda10:	2a0003e5 	mov	w5, w0
ffff8000089eda14:	f94690e0 	ldr	x0, [x7, #3360]
ffff8000089eda18:	52802101 	mov	w1, #0x108                 	// #264
ffff8000089eda1c:	97fffe66 	bl	0xffff8000089ed3b4
ffff8000089eda20:	2a0003e6 	mov	w6, w0
ffff8000089eda24:	aa0703e1 	mov	x1, x7
ffff8000089eda28:	aa0803e0 	mov	x0, x8
ffff8000089eda2c:	97f0f7c3 	bl	0xffff80000862b938
ffff8000089eda30:	52800000 	mov	w0, #0x0                   	// #0
ffff8000089eda34:	a8c17bfd 	ldp	x29, x30, [sp], #16
ffff8000089eda38:	d50323bf 	autiasp
ffff8000089eda3c:	d65f03c0 	ret
ffff8000089eda40:	528007e3 	mov	w3, #0x3f                  	// #63
ffff8000089eda44:	17ffffc3 	b	0xffff8000089ed950
ffff8000089eda48:	52800020 	mov	w0, #0x1                   	// #1
ffff8000089eda4c:	17ffffdc 	b	0xffff8000089ed9bc
ffff8000089eda50:	52800000 	mov	w0, #0x0                   	// #0
ffff8000089eda54:	d65f03c0 	ret
