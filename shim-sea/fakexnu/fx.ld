ENTRY(_start)
SECTIONS
{
	. = 0xfffffe0007004000;
	.text : { *(.text.start) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	_edata = .;
	. = ALIGN(16);
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	. += 0x8000;
	_stack_top = .;
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
