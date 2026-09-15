
docs/verification/evidence/p6_driver_review_20260914/target_code/netif_stop_queue.isra.0.bin:     file format binary


Disassembly of section .data:

ffff8000089edc98 <.data>:
ffff8000089edc98:	d503233f 	paciasp
ffff8000089edc9c:	9102a000 	add	x0, x0, #0xa8
ffff8000089edca0:	f9800011 	prfm	pstl1strm, [x0]
ffff8000089edca4:	c85f7c01 	ldxr	x1, [x0]
ffff8000089edca8:	b2400021 	orr	x1, x1, #0x1
ffff8000089edcac:	c8027c01 	stxr	w2, x1, [x0]
ffff8000089edcb0:	35ffffa2 	cbnz	w2, 0xffff8000089edca4
ffff8000089edcb4:	d50323bf 	autiasp
ffff8000089edcb8:	d65f03c0 	ret
