
docs/verification/evidence/p6_driver_review_20260914/target_code/rockchip_canfd_stop.isra.0.bin:     file format binary


Disassembly of section .data:

ffff8000089ed45c <.data>:
ffff8000089ed45c:	d503233f 	paciasp
ffff8000089ed460:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
ffff8000089ed464:	52800081 	mov	w1, #0x4                   	// #4
ffff8000089ed468:	910003fd 	mov	x29, sp
ffff8000089ed46c:	a90153f3 	stp	x19, x20, [sp, #16]
ffff8000089ed470:	aa0003f3 	mov	x19, x0
ffff8000089ed474:	b90ad001 	str	w1, [x0, #2768]
ffff8000089ed478:	97ffffd8 	bl	0xffff8000089ed3d8
ffff8000089ed47c:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089ed480:	91004000 	add	x0, x0, #0x10
ffff8000089ed484:	d50332bf 	dmb	oshst
ffff8000089ed488:	529fffe1 	mov	w1, #0xffff                	// #65535
ffff8000089ed48c:	b9000001 	str	w1, [x0]
ffff8000089ed490:	9000e7a6 	adrp	x6, 0xffff80000a6e1000
ffff8000089ed494:	9108c0c6 	add	x6, x6, #0x230
ffff8000089ed498:	39412cc0 	ldrb	w0, [x6, #75]
ffff8000089ed49c:	36000240 	tbz	w0, #0, 0xffff8000089ed4e4
ffff8000089ed4a0:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089ed4a4:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ed4a8:	9100a0c6 	add	x6, x6, #0x28
ffff8000089ed4ac:	b00068e3 	adrp	x3, 0xffff80000970a000
ffff8000089ed4b0:	9105a063 	add	x3, x3, #0x168
ffff8000089ed4b4:	b0009622 	adrp	x2, 0xffff800009cb2000
ffff8000089ed4b8:	97ffffbf 	bl	0xffff8000089ed3b4
ffff8000089ed4bc:	2a0003e4 	mov	w4, w0
ffff8000089ed4c0:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089ed4c4:	52800201 	mov	w1, #0x10                  	// #16
ffff8000089ed4c8:	91003c63 	add	x3, x3, #0xf
ffff8000089ed4cc:	91377442 	add	x2, x2, #0xddd
ffff8000089ed4d0:	97ffffb9 	bl	0xffff8000089ed3b4
ffff8000089ed4d4:	2a0003e5 	mov	w5, w0
ffff8000089ed4d8:	aa1303e1 	mov	x1, x19
ffff8000089ed4dc:	aa0603e0 	mov	x0, x6
ffff8000089ed4e0:	97f0f916 	bl	0xffff80000862b938
ffff8000089ed4e4:	a94153f3 	ldp	x19, x20, [sp, #16]
ffff8000089ed4e8:	a8c27bfd 	ldp	x29, x30, [sp], #32
ffff8000089ed4ec:	d50323bf 	autiasp
ffff8000089ed4f0:	d65f03c0 	ret
