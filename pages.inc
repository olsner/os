; vim:filetype=nasm:

%macro respage 0-1 1
	resb (4096*%1)
%endmacro

struc pages, 0x100000
.kernel		respage 3

.page_tables:
.pml4		respage
.low_pdp	respage
.low_pd		respage
.low_pt		respage
.page_tables_end:

.kernel_stack	respage
.kernel_stack_end:

.kernel_pdp	respage
%if use_1gb_pages == 0
.kernel_pd	respage
%endif
.gseg_cpu0	respage
endstruc

kernel_stack_end equ pages.kernel_stack_end
kernel_reserved_end equ pages + pages_size
free_mem_start equ kernel_reserved_end

kernel_base equ -(1 << 30)
