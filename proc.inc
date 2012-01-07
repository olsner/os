; vim:ts=8:sts=8:sw=8:filetype=nasm:

%assign i 0

%macro	reglabels 1-*
%rep	%0
	.r %+ %1 equ .regs+(i * 8)
	%assign i i+1
	%rotate 1
%endrep
%endmacro

struc	proc, -0x80
	.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15

	; Aliases for offsets into regs
	reglabels ax,cx,dx,bx,sp,bp,si,di
%rep 8
	reglabels i
%endrep

	.rip	resq 1
	.endregs equ .rip
	.rflags	resq 1
	.flags	resq 1 ; See PROC_*
	; Pointer to the process we're waiting for (if any). See flags below.
	.waiting_for resq 1
	; Pointer either to the first process waiting on this process.
	.waiters resq 1
	; If/when in a list, points to next process in list
	.next	resq 1
	.cr3	resq 1
	.aspace	resq 1

	align 16
	.fxsave	resb	512
endstruc

; Jump to CPL 0 instead of CPL 3. In reality unused. May be required for
; device drivers?
PROC_KERNEL	equ	0
; Return to user-mode with sysret, only some registers will be restored:
; rsp, rip: restored to previous values
; rcx, r11: rip and rflags, respectively
; rax: syscall return value
; Remaining registers will be 0 (?)
PROC_FASTRET	equ	1
; Similar to FASTRET but we can make a message-send rendezvous immediately
PROC_IN_RECV	equ	2
; Process is trying to do a synchronous send or sendrcv, blocking on the
; waiting_for process to reach a PROC_IN_RECV state.
PROC_IN_SEND	equ	4
