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

%macro SPIN_LOCK 1
%%spin:
lock	bts	dword %1, 1
	jc	%%spin
%endmacro

%macro SPIN_UNLOCK 1
	mov	byte %1, 0
%endmacro

%macro reslock 0-1 1
	resb %1
%endmacro
