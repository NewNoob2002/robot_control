
docs/verification/evidence/p6_driver_review_20260914/target_code/rockchip_canfd_start_xmit.bin:     file format binary


Disassembly of section .data:

ffff8000089ee600 <.data>:
ffff8000089ee600:	d503233f 	paciasp
ffff8000089ee604:	a9bc7bfd 	stp	x29, x30, [sp, #-64]!
ffff8000089ee608:	910003fd 	mov	x29, sp
ffff8000089ee60c:	a90153f3 	stp	x19, x20, [sp, #16]
ffff8000089ee610:	aa0103f4 	mov	x20, x1
ffff8000089ee614:	aa0003e1 	mov	x1, x0
ffff8000089ee618:	a9025bf5 	stp	x21, x22, [sp, #32]
ffff8000089ee61c:	aa0003f6 	mov	x22, x0
ffff8000089ee620:	a90363f7 	stp	x23, x24, [sp, #48]
ffff8000089ee624:	f9406815 	ldr	x21, [x0, #208]
ffff8000089ee628:	aa1403e0 	mov	x0, x20
ffff8000089ee62c:	97ffec26 	bl	0xffff8000089e96c4
ffff8000089ee630:	72001c1f 	tst	w0, #0xff
ffff8000089ee634:	54000ee1 	b.ne	0xffff8000089ee810  // b.any
ffff8000089ee638:	f941e280 	ldr	x0, [x20, #960]
ffff8000089ee63c:	97fffd97 	bl	0xffff8000089edc98
ffff8000089ee640:	f9469280 	ldr	x0, [x20, #3360]
ffff8000089ee644:	52800081 	mov	w1, #0x4                   	// #4
ffff8000089ee648:	97fffb5b 	bl	0xffff8000089ed3b4
ffff8000089ee64c:	b94002b7 	ldr	w23, [x21]
ffff8000089ee650:	12000018 	and	w24, w0, #0x1
ffff8000089ee654:	11000718 	add	w24, w24, #0x1
ffff8000089ee658:	36f80eb7 	tbz	w23, #31, 0xffff8000089ee82c
ffff8000089ee65c:	394012a0 	ldrb	w0, [x21, #4]
ffff8000089ee660:	120072f7 	and	w23, w23, #0x1fffffff
ffff8000089ee664:	97fff171 	bl	0xffff8000089eac28
ffff8000089ee668:	12000c02 	and	w2, w0, #0xf
ffff8000089ee66c:	b94002a1 	ldr	w1, [x21]
ffff8000089ee670:	32190040 	orr	w0, w2, #0x80
ffff8000089ee674:	321a0442 	orr	w2, w2, #0xc0
ffff8000089ee678:	91280293 	add	x19, x20, #0xa00
ffff8000089ee67c:	f262003f 	tst	x1, #0x40000000
ffff8000089ee680:	1a801042 	csel	w2, w2, w0, ne	// ne = any
ffff8000089ee684:	b940d660 	ldr	w0, [x19, #212]
ffff8000089ee688:	362801a0 	tbz	w0, #5, 0xffff8000089ee6bc
ffff8000089ee68c:	b94072c0 	ldr	w0, [x22, #112]
ffff8000089ee690:	7101201f 	cmp	w0, #0x48
ffff8000089ee694:	54000141 	b.ne	0xffff8000089ee6bc  // b.any
ffff8000089ee698:	f9406ac0 	ldr	x0, [x22, #208]
ffff8000089ee69c:	39401000 	ldrb	w0, [x0, #4]
ffff8000089ee6a0:	7101001f 	cmp	w0, #0x40
ffff8000089ee6a4:	540000c8 	b.hi	0xffff8000089ee6bc  // b.pmore
ffff8000089ee6a8:	394016a1 	ldrb	w1, [x21, #5]
ffff8000089ee6ac:	321b0040 	orr	w0, w2, #0x20
ffff8000089ee6b0:	321c0442 	orr	w2, w2, #0x30
ffff8000089ee6b4:	f240003f 	tst	x1, #0x1
ffff8000089ee6b8:	1a801042 	csel	w2, w2, w0, ne	// ne = any
ffff8000089ee6bc:	394d0260 	ldrb	w0, [x19, #832]
ffff8000089ee6c0:	34000c40 	cbz	w0, 0xffff8000089ee848
ffff8000089ee6c4:	f9419a60 	ldr	x0, [x19, #816]
ffff8000089ee6c8:	f100081f 	cmp	x0, #0x2
ffff8000089ee6cc:	54000be8 	b.hi	0xffff8000089ee848  // b.pmore
ffff8000089ee6d0:	b94002a0 	ldr	w0, [x21]
ffff8000089ee6d4:	36f80ba0 	tbz	w0, #31, 0xffff8000089ee848
ffff8000089ee6d8:	f9469280 	ldr	x0, [x20, #3360]
ffff8000089ee6dc:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee6e0:	97fffb35 	bl	0xffff8000089ed3b4
ffff8000089ee6e4:	321b0000 	orr	w0, w0, #0x20
ffff8000089ee6e8:	f9419261 	ldr	x1, [x19, #800]
ffff8000089ee6ec:	d50332bf 	dmb	oshst
ffff8000089ee6f0:	b9000020 	str	w0, [x1]
ffff8000089ee6f4:	394d0260 	ldrb	w0, [x19, #832]
ffff8000089ee6f8:	35000c40 	cbnz	w0, 0xffff8000089ee880
ffff8000089ee6fc:	f9419a60 	ldr	x0, [x19, #816]
ffff8000089ee700:	f100081f 	cmp	x0, #0x2
ffff8000089ee704:	54000be8 	b.hi	0xffff8000089ee880  // b.pmore
ffff8000089ee708:	b94002a0 	ldr	w0, [x21]
ffff8000089ee70c:	36f80ba0 	tbz	w0, #31, 0xffff8000089ee880
ffff8000089ee710:	d53b4238 	mrs	x24, daif
ffff8000089ee714:	12190300 	and	w0, w24, #0x80
ffff8000089ee718:	35000120 	cbnz	w0, 0xffff8000089ee73c
ffff8000089ee71c:	b000ef00 	adrp	x0, 0xffff80000a7cf000
ffff8000089ee720:	52801401 	mov	w1, #0xa0                  	// #160
ffff8000089ee724:	b94a8c00 	ldr	w0, [x0, #2700]
ffff8000089ee728:	7100001f 	cmp	w0, #0x0
ffff8000089ee72c:	52800c00 	mov	w0, #0x60                  	// #96
ffff8000089ee730:	1a81d000 	csel	w0, w0, w1, le
ffff8000089ee734:	92401c00 	and	x0, x0, #0xff
ffff8000089ee738:	d50343df 	msr	daifset, #0x3
ffff8000089ee73c:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee740:	b9434a61 	ldr	w1, [x19, #840]
ffff8000089ee744:	91081000 	add	x0, x0, #0x204
ffff8000089ee748:	d50332bf 	dmb	oshst
ffff8000089ee74c:	b9000001 	str	w1, [x0]
ffff8000089ee750:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee754:	b9434661 	ldr	w1, [x19, #836]
ffff8000089ee758:	91080000 	add	x0, x0, #0x200
ffff8000089ee75c:	d50332bf 	dmb	oshst
ffff8000089ee760:	b9000001 	str	w1, [x0]
ffff8000089ee764:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee768:	b9434e61 	ldr	w1, [x19, #844]
ffff8000089ee76c:	91082000 	add	x0, x0, #0x208
ffff8000089ee770:	d50332bf 	dmb	oshst
ffff8000089ee774:	b9000001 	str	w1, [x0]
ffff8000089ee778:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee77c:	b9435261 	ldr	w1, [x19, #848]
ffff8000089ee780:	91083000 	add	x0, x0, #0x20c
ffff8000089ee784:	d50332bf 	dmb	oshst
ffff8000089ee788:	b9000001 	str	w1, [x0]
ffff8000089ee78c:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee790:	91001000 	add	x0, x0, #0x4
ffff8000089ee794:	d50332bf 	dmb	oshst
ffff8000089ee798:	52800021 	mov	w1, #0x1                   	// #1
ffff8000089ee79c:	b9000001 	str	w1, [x0]
ffff8000089ee7a0:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee7a4:	91081000 	add	x0, x0, #0x204
ffff8000089ee7a8:	d50332bf 	dmb	oshst
ffff8000089ee7ac:	b9000017 	str	w23, [x0]
ffff8000089ee7b0:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee7b4:	91080000 	add	x0, x0, #0x200
ffff8000089ee7b8:	d50332bf 	dmb	oshst
ffff8000089ee7bc:	b9000002 	str	w2, [x0]
ffff8000089ee7c0:	d2800000 	mov	x0, #0x0                   	// #0
ffff8000089ee7c4:	394012a1 	ldrb	w1, [x21, #4]
ffff8000089ee7c8:	6b00003f 	cmp	w1, w0
ffff8000089ee7cc:	5400048c 	b.gt	0xffff8000089ee85c
ffff8000089ee7d0:	aa1403e1 	mov	x1, x20
ffff8000089ee7d4:	aa1603e0 	mov	x0, x22
ffff8000089ee7d8:	52800003 	mov	w3, #0x0                   	// #0
ffff8000089ee7dc:	52800002 	mov	w2, #0x0                   	// #0
ffff8000089ee7e0:	97ffec14 	bl	0xffff8000089e9830
ffff8000089ee7e4:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee7e8:	91001000 	add	x0, x0, #0x4
ffff8000089ee7ec:	d50332bf 	dmb	oshst
ffff8000089ee7f0:	52800041 	mov	w1, #0x2                   	// #2
ffff8000089ee7f4:	b9000001 	str	w1, [x0]
ffff8000089ee7f8:	d51b4238 	msr	daif, x24
ffff8000089ee7fc:	b000ef00 	adrp	x0, 0xffff80000a7cf000
ffff8000089ee800:	b94aa000 	ldr	w0, [x0, #2720]
ffff8000089ee804:	7100001f 	cmp	w0, #0x0
ffff8000089ee808:	5400004d 	b.le	0xffff8000089ee810
ffff8000089ee80c:	d5033f9f 	dsb	sy
ffff8000089ee810:	52800000 	mov	w0, #0x0                   	// #0
ffff8000089ee814:	a94153f3 	ldp	x19, x20, [sp, #16]
ffff8000089ee818:	a9425bf5 	ldp	x21, x22, [sp, #32]
ffff8000089ee81c:	a94363f7 	ldp	x23, x24, [sp, #48]
ffff8000089ee820:	a8c47bfd 	ldp	x29, x30, [sp], #64
ffff8000089ee824:	d50323bf 	autiasp
ffff8000089ee828:	d65f03c0 	ret
ffff8000089ee82c:	394012a0 	ldrb	w0, [x21, #4]
ffff8000089ee830:	12002af7 	and	w23, w23, #0x7ff
ffff8000089ee834:	97fff0fd 	bl	0xffff8000089eac28
ffff8000089ee838:	12000c00 	and	w0, w0, #0xf
ffff8000089ee83c:	b94002a1 	ldr	w1, [x21]
ffff8000089ee840:	321a0002 	orr	w2, w0, #0x40
ffff8000089ee844:	17ffff8d 	b	0xffff8000089ee678
ffff8000089ee848:	f9469280 	ldr	x0, [x20, #3360]
ffff8000089ee84c:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee850:	97fffad9 	bl	0xffff8000089ed3b4
ffff8000089ee854:	121a7800 	and	w0, w0, #0xffffffdf
ffff8000089ee858:	17ffffa4 	b	0xffff8000089ee6e8
ffff8000089ee85c:	8b20c2a1 	add	x1, x21, w0, sxtw
ffff8000089ee860:	91082003 	add	x3, x0, #0x208
ffff8000089ee864:	b9400822 	ldr	w2, [x1, #8]
ffff8000089ee868:	f9419261 	ldr	x1, [x19, #800]
ffff8000089ee86c:	8b030021 	add	x1, x1, x3
ffff8000089ee870:	d50332bf 	dmb	oshst
ffff8000089ee874:	b9000022 	str	w2, [x1]
ffff8000089ee878:	91001000 	add	x0, x0, #0x4
ffff8000089ee87c:	17ffffd2 	b	0xffff8000089ee7c4
ffff8000089ee880:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee884:	91081000 	add	x0, x0, #0x204
ffff8000089ee888:	d50332bf 	dmb	oshst
ffff8000089ee88c:	b9000017 	str	w23, [x0]
ffff8000089ee890:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee894:	91080000 	add	x0, x0, #0x200
ffff8000089ee898:	d50332bf 	dmb	oshst
ffff8000089ee89c:	b9000002 	str	w2, [x0]
ffff8000089ee8a0:	d2800000 	mov	x0, #0x0                   	// #0
ffff8000089ee8a4:	394012a1 	ldrb	w1, [x21, #4]
ffff8000089ee8a8:	6b00003f 	cmp	w1, w0
ffff8000089ee8ac:	540003cc 	b.gt	0xffff8000089ee924
ffff8000089ee8b0:	aa1403e1 	mov	x1, x20
ffff8000089ee8b4:	aa1603e0 	mov	x0, x22
ffff8000089ee8b8:	52800003 	mov	w3, #0x0                   	// #0
ffff8000089ee8bc:	52800002 	mov	w2, #0x0                   	// #0
ffff8000089ee8c0:	97ffebdc 	bl	0xffff8000089e9830
ffff8000089ee8c4:	f9469280 	ldr	x0, [x20, #3360]
ffff8000089ee8c8:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee8cc:	97fffaba 	bl	0xffff8000089ed3b4
ffff8000089ee8d0:	32140000 	orr	w0, w0, #0x1000
ffff8000089ee8d4:	f9419261 	ldr	x1, [x19, #800]
ffff8000089ee8d8:	d50332bf 	dmb	oshst
ffff8000089ee8dc:	b9000020 	str	w0, [x1]
ffff8000089ee8e0:	f9419260 	ldr	x0, [x19, #800]
ffff8000089ee8e4:	91001000 	add	x0, x0, #0x4
ffff8000089ee8e8:	d50332bf 	dmb	oshst
ffff8000089ee8ec:	b9000018 	str	w24, [x0]
ffff8000089ee8f0:	f9469280 	ldr	x0, [x20, #3360]
ffff8000089ee8f4:	52800001 	mov	w1, #0x0                   	// #0
ffff8000089ee8f8:	97fffaaf 	bl	0xffff8000089ed3b4
ffff8000089ee8fc:	12137800 	and	w0, w0, #0xffffefff
ffff8000089ee900:	f9419261 	ldr	x1, [x19, #800]
ffff8000089ee904:	d50332bf 	dmb	oshst
ffff8000089ee908:	b9000020 	str	w0, [x1]
ffff8000089ee90c:	b943b260 	ldr	w0, [x19, #944]
ffff8000089ee910:	97dc6af2 	bl	0xffff8000081094d8
ffff8000089ee914:	aa0003e1 	mov	x1, x0
ffff8000089ee918:	91356280 	add	x0, x20, #0xd58
ffff8000089ee91c:	97ffff2d 	bl	0xffff8000089ee5d0
ffff8000089ee920:	17ffffbc 	b	0xffff8000089ee810
ffff8000089ee924:	8b20c2a1 	add	x1, x21, w0, sxtw
ffff8000089ee928:	91082003 	add	x3, x0, #0x208
ffff8000089ee92c:	b9400822 	ldr	w2, [x1, #8]
ffff8000089ee930:	f9419261 	ldr	x1, [x19, #800]
ffff8000089ee934:	8b030021 	add	x1, x1, x3
ffff8000089ee938:	d50332bf 	dmb	oshst
ffff8000089ee93c:	b9000022 	str	w2, [x1]
ffff8000089ee940:	91001000 	add	x0, x0, #0x4
ffff8000089ee944:	17ffffd8 	b	0xffff8000089ee8a4
