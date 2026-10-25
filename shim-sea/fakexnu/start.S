	.section .text.start
	.globl _start
_start:
	mov	x19, x0			/* boot_args (physical) */
	adrp	x1, _stack_top
	add	x1, x1, :lo12:_stack_top
	mov	sp, x1
	mov	x0, x19
	bl	fx_main
1:	wfe
	b	1b
