ENTRY(start)
OUTPUT_FORMAT(binary)

SECTIONS
{
	. = 1M;

	.text : {
		*(.start);
		*(.text*);
	}
	.rodata : {
		*(.rodata*);
	}
	/DISCARD/ : {
		*(.eh_frame);
		*(.comment);
		*(.note*);
	}
}
