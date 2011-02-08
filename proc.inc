; vim:ts=8:sts=8:sw=8:filetype=nasm:

struc	proc, -0x80
	.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15

	; Aliases for offsets into regs
	.rax	equ	.regs+(0*8)
	.rcx	equ	.regs+(1*8)
	.rbx	equ	.regs+(3*8)
	.rsp	equ	.regs+(4*8)
	.rbp	equ	.regs+(5*8)
	.rsi	equ	.regs+(6*8)
	.rdi	equ	.regs+(7*8)
	%assign i 8
	%rep 8
	.r%+i	equ	.regs+(i*8)
	%assign i i+1
	%endrep

	.rip	resq 1
	.endregs equ .rip
	.rflags	resq 1
	.flags	resq 1 ; See PROC_*
	.waiting_for resq 1 ; Pointer to proc
	.next	resq 1 ; If/when in a list, points to next process in list
	.cr3	resq 1
endstruc

; Jump to CPL 0 instead of CPL 3
PROC_KERNEL	equ	0
; Return to user-mode with sysret, only some registers will be restored:
; rsp, rip: restored to previous values
; rcx, r11: rip and rflags, respectively
; rax: syscall return value
; Remaining registers will be 0 (?)
PROC_FASTRET	equ	1

