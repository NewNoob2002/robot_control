
docs/verification/evidence/p6_driver_review_20260914/target_code/rockchip_canfd_close.bin:     file format binary


Disassembly of section .data:

ffff8000089edcbc <.data>:
ffff8000089edcbc:	d503233f 	paciasp
ffff8000089edcc0:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
ffff8000089edcc4:	910003fd 	mov	x29, sp
ffff8000089edcc8:	a90153f3 	stp	x19, x20, [sp, #16]
ffff8000089edccc:	91280014 	add	x20, x0, #0xa00
ffff8000089edcd0:	aa0003f3 	mov	x19, x0
ffff8000089edcd4:	f941e000 	ldr	x0, [x0, #960]
ffff8000089edcd8:	97fffff0 	bl	0xffff8000089edc98
ffff8000089edcdc:	f9419a80 	ldr	x0, [x20, #816]
ffff8000089edce0:	f1000c1f 	cmp	x0, #0x3
ffff8000089edce4:	54000061 	b.ne	0xffff8000089edcf0  // b.any
ffff8000089edce8:	912de260 	add	x0, x19, #0xb78
ffff8000089edcec:	94105025 	bl	0xffff800008e01d80
ffff8000089edcf0:	aa1303e0 	mov	x0, x19
ffff8000089edcf4:	97fffdda 	bl	0xffff8000089ed45c
ffff8000089edcf8:	aa1303e0 	mov	x0, x19
ffff8000089edcfc:	97fff1e1 	bl	0xffff8000089ea480
ffff8000089edd00:	f940ba80 	ldr	x0, [x20, #368]
ffff8000089edd04:	97ffffdd 	bl	0xffff8000089edc78
ffff8000089edd08:	91356260 	add	x0, x19, #0xd58
ffff8000089edd0c:	97dab199 	bl	0xffff80000809a370
ffff8000089edd10:	9000e7a0 	adrp	x0, 0xffff80000a6e1000
ffff8000089edd14:	9108c000 	add	x0, x0, #0x230
ffff8000089edd18:	9103c000 	add	x0, x0, #0xf0
ffff8000089edd1c:	39408c01 	ldrb	w1, [x0, #35]
ffff8000089edd20:	36000101 	tbz	w1, #0, 0xffff8000089edd40
ffff8000089edd24:	b00068e3 	adrp	x3, 0xffff80000970a000
ffff8000089edd28:	9105a063 	add	x3, x3, #0x168
ffff8000089edd2c:	900095e2 	adrp	x2, 0xffff800009ca9000
ffff8000089edd30:	91019463 	add	x3, x3, #0x65
ffff8000089edd34:	913a6442 	add	x2, x2, #0xe99
ffff8000089edd38:	aa1303e1 	mov	x1, x19
ffff8000089edd3c:	97f0f6ff 	bl	0xffff80000862b938
ffff8000089edd40:	52800000 	mov	w0, #0x0                   	// #0
ffff8000089edd44:	a94153f3 	ldp	x19, x20, [sp, #16]
ffff8000089edd48:	a8c27bfd 	ldp	x29, x30, [sp], #32
ffff8000089edd4c:	d50323bf 	autiasp
ffff8000089edd50:	d65f03c0 	ret
