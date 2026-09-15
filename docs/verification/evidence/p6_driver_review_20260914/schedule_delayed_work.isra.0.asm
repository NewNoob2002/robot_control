
docs/verification/evidence/p6_driver_review_20260914/target_code/schedule_delayed_work.isra.0.bin:     file format binary


Disassembly of section .data:

ffff8000089ee5d0 <.data>:
ffff8000089ee5d0:	d503233f 	paciasp
ffff8000089ee5d4:	aa0003e2 	mov	x2, x0
ffff8000089ee5d8:	a9bf7bfd 	stp	x29, x30, [sp, #-16]!
ffff8000089ee5dc:	d000cda0 	adrp	x0, 0xffff80000a3a4000
ffff8000089ee5e0:	aa0103e3 	mov	x3, x1
ffff8000089ee5e4:	910003fd 	mov	x29, sp
ffff8000089ee5e8:	f9453001 	ldr	x1, [x0, #2656]
ffff8000089ee5ec:	52800100 	mov	w0, #0x8                   	// #8
ffff8000089ee5f0:	97daa9a6 	bl	0xffff800008098c88
ffff8000089ee5f4:	a8c17bfd 	ldp	x29, x30, [sp], #16
ffff8000089ee5f8:	d50323bf 	autiasp
ffff8000089ee5fc:	d65f03c0 	ret
