
docs/verification/evidence/p6_driver_review_20260914/target_code/rockchip_canfd_tx_err_delay_work.bin:     file format binary


Disassembly of section .data:

ffff8000089ee948 <.data>:
ffff8000089ee948:	d503233f 	paciasp
ffff8000089ee94c:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
ffff8000089ee950:	d10d6002 	sub	x2, x0, #0x358
ffff8000089ee954:	910003fd 	mov	x29, sp
ffff8000089ee958:	f9000bf3 	str	x19, [sp, #16]
ffff8000089ee95c:	aa0003f3 	mov	x19, x0
ffff8000089ee960:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee964:	f85c8000 	ldur	x0, [x0, #-56]
ffff8000089ee968:	97fffa93 	bl	0xffff8000089ed3b4
ffff8000089ee96c:	2a0003e3 	mov	w3, w0
ffff8000089ee970:	f85c8260 	ldur	x0, [x19, #-56]
ffff8000089ee974:	52800581 	mov	w1, #0x2c                  	// #44
ffff8000089ee978:	97fffa8f 	bl	0xffff8000089ed3b4
ffff8000089ee97c:	52810001 	mov	w1, #0x800                 	// #2048
ffff8000089ee980:	72a18401 	movk	w1, #0xc20, lsl #16
ffff8000089ee984:	6a20003f 	bics	wzr, w1, w0
ffff8000089ee988:	540003a1 	b.ne	0xffff8000089ee9fc  // b.any
ffff8000089ee98c:	f85c8260 	ldur	x0, [x19, #-56]
ffff8000089ee990:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee994:	97fffa88 	bl	0xffff8000089ed3b4
ffff8000089ee998:	32140000 	orr	w0, w0, #0x1000
ffff8000089ee99c:	f9419041 	ldr	x1, [x2, #800]
ffff8000089ee9a0:	d50332bf 	dmb	oshst
ffff8000089ee9a4:	b9000020 	str	w0, [x1]
ffff8000089ee9a8:	f9419040 	ldr	x0, [x2, #800]
ffff8000089ee9ac:	91001000 	add	x0, x0, #0x4
ffff8000089ee9b0:	d50332bf 	dmb	oshst
ffff8000089ee9b4:	52800021 	mov	w1, #0x1                   	// #1
ffff8000089ee9b8:	b9000001 	str	w1, [x0]
ffff8000089ee9bc:	f85c8260 	ldur	x0, [x19, #-56]
ffff8000089ee9c0:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee9c4:	97fffa7c 	bl	0xffff8000089ed3b4
ffff8000089ee9c8:	12137800 	and	w0, w0, #0xffffefff
ffff8000089ee9cc:	f9419041 	ldr	x1, [x2, #800]
ffff8000089ee9d0:	d50332bf 	dmb	oshst
ffff8000089ee9d4:	b9000020 	str	w0, [x1]
ffff8000089ee9d8:	b943b040 	ldr	w0, [x2, #944]
ffff8000089ee9dc:	97dc6abf 	bl	0xffff8000081094d8
ffff8000089ee9e0:	aa0003e1 	mov	x1, x0
ffff8000089ee9e4:	aa1303e0 	mov	x0, x19
ffff8000089ee9e8:	97fffefa 	bl	0xffff8000089ee5d0
ffff8000089ee9ec:	f9400bf3 	ldr	x19, [sp, #16]
ffff8000089ee9f0:	a8c27bfd 	ldp	x29, x30, [sp], #32
ffff8000089ee9f4:	d50323bf 	autiasp
ffff8000089ee9f8:	d65f03c0 	ret
ffff8000089ee9fc:	f9419040 	ldr	x0, [x2, #800]
ffff8000089eea00:	d50332bf 	dmb	oshst
ffff8000089eea04:	b900001f 	str	wzr, [x0]
ffff8000089eea08:	f9419040 	ldr	x0, [x2, #800]
ffff8000089eea0c:	d50332bf 	dmb	oshst
ffff8000089eea10:	b9000003 	str	w3, [x0]
ffff8000089eea14:	17ffffde 	b	0xffff8000089ee98c
