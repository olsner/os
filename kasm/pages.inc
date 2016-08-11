; vim:filetype=nasm:

%macro respage 0-1 1
	resb (4096*%1)
%endmacro

; Includes the .bss section (really, the pages. stuff should actually be in the
; .bss section I think). Though it is required by the 32-bit bootstrap as well,
; so it needs well-defined addresses.
%ifndef kernel_pages
%define kernel_pages 4
%endif

struc pages, 0x100000
.kernel		respage kernel_pages

.page_tables:
.pml4		respage
.low_pdp	respage
.low_pd		respage
.kernel_pdp	respage
%if use_1gb_pages == 0
.kernel_pd	respage
%endif
.page_tables_end:

; Annoying: BSP needs a stack quite early in the bootstrap, so it needs to be
; static, but APs will always use a heap-allocated stack. It would be nice to
; unify it and always heap-allocate the stack.
; (Can't *call* allocate_frame without a stack...)
.kernel_stack	respage
.kernel_stack_end:

.stats		respage
endstruc

kernel_stack_end equ pages.kernel_stack_end
kernel_reserved_end equ pages + pages_size
free_mem_start equ kernel_reserved_end

kernel_base equ -(1 << 30)
