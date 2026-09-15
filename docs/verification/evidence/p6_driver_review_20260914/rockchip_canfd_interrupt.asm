
docs/verification/evidence/p6_driver_review_20260914/target_code/rockchip_canfd_interrupt.bin:     file format binary


Disassembly of section .data:

ffff8000089edd7c <.data>:
ffff8000089edd7c:	d503233f 	paciasp
ffff8000089edd80:	d10143ff 	sub	sp, sp, #0x50
ffff8000089edd84:	d5384100 	mrs	x0, sp_el0
ffff8000089edd88:	a9017bfd 	stp	x29, x30, [sp, #16]
ffff8000089edd8c:	910043fd 	add	x29, sp, #0x10
ffff8000089edd90:	a90253f3 	stp	x19, x20, [sp, #32]
ffff8000089edd94:	aa0103f3 	mov	x19, x1
ffff8000089edd98:	91280034 	add	x20, x1, #0xa00
ffff8000089edd9c:	a9035bf5 	stp	x21, x22, [sp, #48]
ffff8000089edda0:	f90023f7 	str	x23, [sp, #64]
ffff8000089edda4:	f9429c01 	ldr	x1, [x0, #1336]
ffff8000089edda8:	f90007e1 	str	x1, [sp, #8]
ffff8000089eddac:	d2800001 	mov	x1, #0x0                   	// #0
ffff8000089eddb0:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089eddb4:	52800181 	mov	w1, #0xc                   	// #12
ffff8000089eddb8:	97fffd7f 	bl	0xffff8000089ed3b4
ffff8000089eddbc:	2a0003f5 	mov	w21, w0
ffff8000089eddc0:	36080875 	tbz	w21, #1, 0xffff8000089edecc
ffff8000089eddc4:	91356260 	add	x0, x19, #0xd58
ffff8000089eddc8:	97dab0f6 	bl	0xffff80000809a1a0
ffff8000089eddcc:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089eddd0:	52804001 	mov	w1, #0x200                 	// #512
ffff8000089eddd4:	97fffd78 	bl	0xffff8000089ed3b4
ffff8000089eddd8:	394d0281 	ldrb	w1, [x20, #832]
ffff8000089edddc:	340002c1 	cbz	w1, 0xffff8000089ede34
ffff8000089edde0:	f9419a81 	ldr	x1, [x20, #816]
ffff8000089edde4:	f100083f 	cmp	x1, #0x2
ffff8000089edde8:	54000268 	b.hi	0xffff8000089ede34  // b.pmore
ffff8000089eddec:	36380240 	tbz	w0, #7, 0xffff8000089ede34
ffff8000089eddf0:	f9419280 	ldr	x0, [x20, #800]
ffff8000089eddf4:	91014000 	add	x0, x0, #0x50
ffff8000089eddf8:	d50332bf 	dmb	oshst
ffff8000089eddfc:	52801001 	mov	w1, #0x80                  	// #128
ffff8000089ede00:	b9000001 	str	w1, [x0]
ffff8000089ede04:	aa1303e0 	mov	x0, x19
ffff8000089ede08:	97fffe46 	bl	0xffff8000089ed720
ffff8000089ede0c:	2a0003f6 	mov	w22, w0
ffff8000089ede10:	35000e20 	cbnz	w0, 0xffff8000089edfd4
ffff8000089ede14:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089ede18:	52800a01 	mov	w1, #0x50                  	// #80
ffff8000089ede1c:	97fffd66 	bl	0xffff8000089ed3b4
ffff8000089ede20:	37000e80 	tbnz	w0, #0, 0xffff8000089edff0
ffff8000089ede24:	f9419280 	ldr	x0, [x20, #800]
ffff8000089ede28:	91014000 	add	x0, x0, #0x50
ffff8000089ede2c:	d50332bf 	dmb	oshst
ffff8000089ede30:	b900001f 	str	wzr, [x0]
ffff8000089ede34:	97dc85e7 	bl	0xffff80000810f5d0
ffff8000089ede38:	d29e4001 	mov	x1, #0xf200                	// #61952
ffff8000089ede3c:	f2a540a1 	movk	x1, #0x2a05, lsl #16
ffff8000089ede40:	f2c00021 	movk	x1, #0x1, lsl #32
ffff8000089ede44:	8b010016 	add	x22, x0, x1
ffff8000089ede48:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089ede4c:	52800081 	mov	w1, #0x4                   	// #4
ffff8000089ede50:	97fffd59 	bl	0xffff8000089ed3b4
ffff8000089ede54:	f240041f 	tst	x0, #0x3
ffff8000089ede58:	540001a0 	b.eq	0xffff8000089ede8c  // b.none
ffff8000089ede5c:	97dc85dd 	bl	0xffff80000810f5d0
ffff8000089ede60:	eb16001f 	cmp	x0, x22
ffff8000089ede64:	54000d2d 	b.le	0xffff8000089ee008
ffff8000089ede68:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089ede6c:	52800081 	mov	w1, #0x4                   	// #4
ffff8000089ede70:	97fffd51 	bl	0xffff8000089ed3b4
ffff8000089ede74:	f240041f 	tst	x0, #0x3
ffff8000089ede78:	540000a0 	b.eq	0xffff8000089ede8c  // b.none
ffff8000089ede7c:	b0009621 	adrp	x1, 0xffff800009cb2000
ffff8000089ede80:	aa1303e0 	mov	x0, x19
ffff8000089ede84:	91391821 	add	x1, x1, #0xe46
ffff8000089ede88:	941d8293 	bl	0xffff80000914e8d4
ffff8000089ede8c:	f9419280 	ldr	x0, [x20, #800]
ffff8000089ede90:	91001000 	add	x0, x0, #0x4
ffff8000089ede94:	d50332bf 	dmb	oshst
ffff8000089ede98:	b900001f 	str	wzr, [x0]
ffff8000089ede9c:	d2800002 	mov	x2, #0x0                   	// #0
ffff8000089edea0:	aa1303e0 	mov	x0, x19
ffff8000089edea4:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089edea8:	97ffef4d 	bl	0xffff8000089e9bdc
ffff8000089edeac:	f940a662 	ldr	x2, [x19, #328]
ffff8000089edeb0:	8b204040 	add	x0, x2, w0, uxtw
ffff8000089edeb4:	f900a660 	str	x0, [x19, #328]
ffff8000089edeb8:	f9409e60 	ldr	x0, [x19, #312]
ffff8000089edebc:	91000400 	add	x0, x0, #0x1
ffff8000089edec0:	f9009e60 	str	x0, [x19, #312]
ffff8000089edec4:	f941e260 	ldr	x0, [x19, #960]
ffff8000089edec8:	94104cb8 	bl	0xffff800008e011a8
ffff8000089edecc:	36000215 	tbz	w21, #0, 0xffff8000089edf0c
ffff8000089eded0:	f9419a81 	ldr	x1, [x20, #816]
ffff8000089eded4:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089eded8:	f1000c3f 	cmp	x1, #0x3
ffff8000089ededc:	540009a1 	b.ne	0xffff8000089ee010  // b.any
ffff8000089edee0:	91004000 	add	x0, x0, #0x10
ffff8000089edee4:	d50332bf 	dmb	oshst
ffff8000089edee8:	52800021 	mov	w1, #0x1                   	// #1
ffff8000089edeec:	b9000001 	str	w1, [x0]
ffff8000089edef0:	912de276 	add	x22, x19, #0xb78
ffff8000089edef4:	aa1603e0 	mov	x0, x22
ffff8000089edef8:	94103f6a 	bl	0xffff800008dfdca0
ffff8000089edefc:	72001c1f 	tst	w0, #0xff
ffff8000089edf00:	54000060 	b.eq	0xffff8000089edf0c  // b.none
ffff8000089edf04:	aa1603e0 	mov	x0, x22
ffff8000089edf08:	94104d68 	bl	0xffff800008e014a8
ffff8000089edf0c:	52804b80 	mov	w0, #0x25c                 	// #604
ffff8000089edf10:	6a0002bf 	tst	w21, w0
ffff8000089edf14:	54000be0 	b.eq	0xffff8000089ee090  // b.none
ffff8000089edf18:	910003e1 	mov	x1, sp
ffff8000089edf1c:	aa1303e0 	mov	x0, x19
ffff8000089edf20:	97ffed74 	bl	0xffff8000089e94f0
ffff8000089edf24:	aa0003f6 	mov	x22, x0
ffff8000089edf28:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089edf2c:	52800681 	mov	w1, #0x34                  	// #52
ffff8000089edf30:	97fffd21 	bl	0xffff8000089ed3b4
ffff8000089edf34:	2a0003e2 	mov	w2, w0
ffff8000089edf38:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089edf3c:	52800701 	mov	w1, #0x38                  	// #56
ffff8000089edf40:	97fffd1d 	bl	0xffff8000089ed3b4
ffff8000089edf44:	2a0003e3 	mov	w3, w0
ffff8000089edf48:	f9469260 	ldr	x0, [x19, #3360]
ffff8000089edf4c:	52800101 	mov	w1, #0x8                   	// #8
ffff8000089edf50:	97fffd19 	bl	0xffff8000089ed3b4
ffff8000089edf54:	b4000096 	cbz	x22, 0xffff8000089edf64
ffff8000089edf58:	f94003e1 	ldr	x1, [sp]
ffff8000089edf5c:	39003823 	strb	w3, [x1, #14]
ffff8000089edf60:	39003c22 	strb	w2, [x1, #15]
ffff8000089edf64:	36480735 	tbz	w21, #9, 0xffff8000089ee048
ffff8000089edf68:	52800061 	mov	w1, #0x3                   	// #3
ffff8000089edf6c:	b900d281 	str	w1, [x20, #208]
ffff8000089edf70:	b9401681 	ldr	w1, [x20, #20]
ffff8000089edf74:	f94003e2 	ldr	x2, [sp]
ffff8000089edf78:	11000421 	add	w1, w1, #0x1
ffff8000089edf7c:	b9001681 	str	w1, [x20, #20]
ffff8000089edf80:	b9400041 	ldr	w1, [x2]
ffff8000089edf84:	321a0021 	orr	w1, w1, #0x40
ffff8000089edf88:	b9000041 	str	w1, [x2]
ffff8000089edf8c:	b940d281 	ldr	w1, [x20, #208]
ffff8000089edf90:	7100083f 	cmp	w1, #0x2
ffff8000089edf94:	54000689 	b.ls	0xffff8000089ee064  // b.plast
ffff8000089edf98:	91356260 	add	x0, x19, #0xd58
ffff8000089edf9c:	97dab081 	bl	0xffff80000809a1a0
ffff8000089edfa0:	f941e260 	ldr	x0, [x19, #960]
ffff8000089edfa4:	97ffff3d 	bl	0xffff8000089edc98
ffff8000089edfa8:	aa1303e0 	mov	x0, x19
ffff8000089edfac:	97fffd2c 	bl	0xffff8000089ed45c
ffff8000089edfb0:	d2800002 	mov	x2, #0x0                   	// #0
ffff8000089edfb4:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089edfb8:	aa1303e0 	mov	x0, x19
ffff8000089edfbc:	97ffed5e 	bl	0xffff8000089e9534
ffff8000089edfc0:	aa1303e0 	mov	x0, x19
ffff8000089edfc4:	97fffea5 	bl	0xffff8000089eda58
ffff8000089edfc8:	f941e260 	ldr	x0, [x19, #960]
ffff8000089edfcc:	97ffff62 	bl	0xffff8000089edd54
ffff8000089edfd0:	14000026 	b	0xffff8000089ee068
ffff8000089edfd4:	52800017 	mov	w23, #0x0                   	// #0
ffff8000089edfd8:	aa1303e0 	mov	x0, x19
ffff8000089edfdc:	97fffd46 	bl	0xffff8000089ed4f4
ffff8000089edfe0:	0b0002f7 	add	w23, w23, w0
ffff8000089edfe4:	6b1702df 	cmp	w22, w23
ffff8000089edfe8:	54ffff88 	b.hi	0xffff8000089edfd8  // b.pmore
ffff8000089edfec:	17ffff8a 	b	0xffff8000089ede14
ffff8000089edff0:	f9419280 	ldr	x0, [x20, #800]
ffff8000089edff4:	91001000 	add	x0, x0, #0x4
ffff8000089edff8:	d50332bf 	dmb	oshst
ffff8000089edffc:	52800041 	mov	w1, #0x2                   	// #2
ffff8000089ee000:	b9000001 	str	w1, [x0]
ffff8000089ee004:	17ffff88 	b	0xffff8000089ede24
ffff8000089ee008:	d503203f 	yield
ffff8000089ee00c:	17ffff8f 	b	0xffff8000089ede48
ffff8000089ee010:	52802301 	mov	w1, #0x118                 	// #280
ffff8000089ee014:	97fffce8 	bl	0xffff8000089ed3b4
ffff8000089ee018:	b9433e81 	ldr	w1, [x20, #828]
ffff8000089ee01c:	0a010016 	and	w22, w0, w1
ffff8000089ee020:	b9433a80 	ldr	w0, [x20, #824]
ffff8000089ee024:	1ac026d6 	lsr	w22, w22, w0
ffff8000089ee028:	34fff736 	cbz	w22, 0xffff8000089edf0c
ffff8000089ee02c:	52800017 	mov	w23, #0x0                   	// #0
ffff8000089ee030:	aa1303e0 	mov	x0, x19
ffff8000089ee034:	97fffd30 	bl	0xffff8000089ed4f4
ffff8000089ee038:	0b0002f7 	add	w23, w23, w0
ffff8000089ee03c:	6b1702df 	cmp	w22, w23
ffff8000089ee040:	54ffff88 	b.hi	0xffff8000089ee030  // b.pmore
ffff8000089ee044:	17ffffb2 	b	0xffff8000089edf0c
ffff8000089ee048:	361003b5 	tbz	w21, #2, 0xffff8000089ee0bc
ffff8000089ee04c:	b9400e81 	ldr	w1, [x20, #12]
ffff8000089ee050:	11000421 	add	w1, w1, #0x1
ffff8000089ee054:	b9000e81 	str	w1, [x20, #12]
ffff8000089ee058:	52800021 	mov	w1, #0x1                   	// #1
ffff8000089ee05c:	b900d281 	str	w1, [x20, #208]
ffff8000089ee060:	b50003b6 	cbnz	x22, 0xffff8000089ee0d4
ffff8000089ee064:	372ff9a0 	tbnz	w0, #5, 0xffff8000089edf98
ffff8000089ee068:	f9409a60 	ldr	x0, [x19, #304]
ffff8000089ee06c:	f940a262 	ldr	x2, [x19, #320]
ffff8000089ee070:	91000400 	add	x0, x0, #0x1
ffff8000089ee074:	f9009a60 	str	x0, [x19, #304]
ffff8000089ee078:	f94003e0 	ldr	x0, [sp]
ffff8000089ee07c:	39401000 	ldrb	w0, [x0, #4]
ffff8000089ee080:	8b020000 	add	x0, x0, x2
ffff8000089ee084:	f900a260 	str	x0, [x19, #320]
ffff8000089ee088:	aa1603e0 	mov	x0, x22
ffff8000089ee08c:	9410592b 	bl	0xffff800008e04538
ffff8000089ee090:	f9419280 	ldr	x0, [x20, #800]
ffff8000089ee094:	91003000 	add	x0, x0, #0xc
ffff8000089ee098:	d50332bf 	dmb	oshst
ffff8000089ee09c:	b9000015 	str	w21, [x0]
ffff8000089ee0a0:	d5384100 	mrs	x0, sp_el0
ffff8000089ee0a4:	f94007e2 	ldr	x2, [sp, #8]
ffff8000089ee0a8:	f9429c01 	ldr	x1, [x0, #1336]
ffff8000089ee0ac:	eb010042 	subs	x2, x2, x1
ffff8000089ee0b0:	d2800001 	mov	x1, #0x0                   	// #0
ffff8000089ee0b4:	540002a0 	b.eq	0xffff8000089ee108  // b.none
ffff8000089ee0b8:	941d8f7a 	bl	0xffff800009151ea0
ffff8000089ee0bc:	3627f695 	tbz	w21, #4, 0xffff8000089edf8c
ffff8000089ee0c0:	b9401281 	ldr	w1, [x20, #16]
ffff8000089ee0c4:	11000421 	add	w1, w1, #0x1
ffff8000089ee0c8:	b9001281 	str	w1, [x20, #16]
ffff8000089ee0cc:	52800041 	mov	w1, #0x2                   	// #2
ffff8000089ee0d0:	b900d281 	str	w1, [x20, #208]
ffff8000089ee0d4:	f94003e4 	ldr	x4, [sp]
ffff8000089ee0d8:	6b03005f 	cmp	w2, w3
ffff8000089ee0dc:	b9400081 	ldr	w1, [x4]
ffff8000089ee0e0:	321e0021 	orr	w1, w1, #0x4
ffff8000089ee0e4:	b9000081 	str	w1, [x4]
ffff8000089ee0e8:	52800081 	mov	w1, #0x4                   	// #4
ffff8000089ee0ec:	52800104 	mov	w4, #0x8                   	// #8
ffff8000089ee0f0:	1a813084 	csel	w4, w4, w1, cc	// cc = lo, ul, last
ffff8000089ee0f4:	f94003e1 	ldr	x1, [sp]
ffff8000089ee0f8:	39002424 	strb	w4, [x1, #9]
ffff8000089ee0fc:	39003823 	strb	w3, [x1, #14]
ffff8000089ee100:	39003c22 	strb	w2, [x1, #15]
ffff8000089ee104:	17ffffa2 	b	0xffff8000089edf8c
ffff8000089ee108:	52800020 	mov	w0, #0x1                   	// #1
ffff8000089ee10c:	a9417bfd 	ldp	x29, x30, [sp, #16]
ffff8000089ee110:	a94253f3 	ldp	x19, x20, [sp, #32]
ffff8000089ee114:	a9435bf5 	ldp	x21, x22, [sp, #48]
ffff8000089ee118:	f94023f7 	ldr	x23, [sp, #64]
ffff8000089ee11c:	910143ff 	add	sp, sp, #0x50
ffff8000089ee120:	d50323bf 	autiasp
ffff8000089ee124:	d65f03c0 	ret
