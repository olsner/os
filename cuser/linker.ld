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
	/* 1MB of .text and initialized .data should be enough for anyone. */
	. = 2M;
	PROVIDE(__bss_start = .);
	.data : AT( ADDR(.text) + SIZEOF(.text) ) {
		PROVIDE(__data_lma = LOADADDR(.data));
		PROVIDE(__data_vma = .);
		. += SIZEOF(.data);
		*(.data*);
		PROVIDE(__data_lma_end = LOADADDR(.data) + SIZEOF(.data));
	}
	/* Note that none of these get loaded properly unless the program does it
	 * by itself.
	 * 1. Map __bss_start .. __bss_end as read-writeable zero-init memory
	 * 2. Copy __data_size bytes from __data_lma to __data_vma. 
	 * __data_vma is inside the bss range just allocated. */
	.bss : {
		*(.bss*);
	}
	PROVIDE(__bss_end = ALIGN(4096));
	PROVIDE(__data_size = SIZEOF(.data));
	.placeholder (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(.placeholder*));
	}
	/DISCARD/ : {
		*(.eh_frame);
		*(.comment);
		*(.note*);
	}
}
