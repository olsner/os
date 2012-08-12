; vim:filetype=nasm:

; callee-save: rbp, rbx, r12-r15
; caller-save: rax, rcx, rdx, rsi, rdi, r8-r11
%macro clear_clobbered_syscall 0
	; rax, rcx, r11 are also in this list, but are used for return, rip and rflags respectively.
	zero	edx ; If we start returning more than one 64-bit value
	zero	esi
	zero	edi
	zero	r8
	zero	r9
	zero	r10
%endmacro
%macro clear_clobbered 0
	clear_clobbered_syscall
	zero	ecx
	zero	r11
%endmacro
; Most internal functions use a special convention where rdi points to the
; cpu-specific data segment. (Maybe we should change this to use e.g. rbp.)
%macro clear_clobbered_keeprdi 0
	zero	ecx
	zero	edx ; If we start returning more than one 64-bit value
	zero	esi
	zero	r8
	zero	r9
	zero	r10
	zero	r11
%endmacro

%macro pushsection 1
[section %1]
%endmacro
%macro popsection 0
__SECT__
%endmacro

%macro zero 1
	xor	%1, %1
%endmacro

%macro restruc 1-2 1
	resb (%1 %+ _size) * %2
%endmacro

