ENTRY(start)
OUTPUT_FORMAT(binary)

SECTIONS
{
	. = 1M;

	.text : {
		*(.start);
		*(.text*);
		*(.rodata*);
	}
	/* TODO bss: page aligned. (It doesn't get allocated by anything - but
	 * it's still useful to let linker handle layout of things that the program
	 * can allocate (e.g. zeropage). */
	/* TODO data: We don't make data writeable so we don't really support it. */
	/* FWIW, it should be possible to add a small loader shim that maps all
	 * .bss pages, copies RW-data pages, and calls the actual entry point. */
	/DISCARD/ : {
		*(.eh_frame);
		*(.comment);
		*(.note*);
	}
}
