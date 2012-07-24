; vim:ts=8:sts=8:sw=8:filetype=nasm:

%macro respage 0-1 1
	resb (4096*%1)
%endmacro

struc pages, 0xa000
.kernel		respage 2
.memory_map	respage

.page_tables:
.pml4		respage
.low_pdp	respage
.low_pd		respage
.low_pt		respage
.page_tables_end:

.kernel_stack	respage
.kernel_stack_end:

.kernel_pdp	respage
.gseg_cpu0	respage
endstruc

kernel_stack_end equ pages.kernel_stack_end
