/* Boot card shown by splash_draw(): u32 w, u32 h, then w*h BGRA pixels (tools: splash/make-splash.py). */
	.section .rodata
	.balign	8
	.globl	splash_bin
splash_bin:
	.incbin	"splash/splash.bin"
	.globl	splash_bin_end
splash_bin_end:
