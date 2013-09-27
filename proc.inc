; vim:filetype=nasm:

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
	; Pointer to the first process waiting on this process.
	.waiters restruc dlist
	; If/when in a list, points to next process in list
	.node	restruc dlist_node
	.cr3	resq 1
	.aspace	resq 1
	.count	resq 1
	; When PROC_PFAULT is set, the virtual address that faulted
	; Note that we lose a lot of data about the mapping that we looked up
	; in PFAULT, and have to look up again in GRANT. This is intentional,
	; since we have to verify and match the GRANT to the correct page, we
	; simply don't save anything that might be wrong.
	; The lower bits are access flags for the fault/request.
	.fault_addr	resq 1

	align 16
	.fxsave	resb	512
endstruc

%macro defbit 2
%1 %+ _BIT	equ	%2
%1		equ	(1 << (%1 %+ _BIT))
%endmacro

; The process is currently queued on the run queue.
defbit	PROC_ON_RUNQUEUE,	0
; Can return to user-mode with sysret, only some registers will be restored:
; rsp, rip: restored to previous values
; rcx, r11: rip and rflags, respectively
; rax: syscall return value
; Remaining registers will be 0 (?)
; TODO where do we set this? do we use it?
defbit	PROC_FASTRET,	1
; IN_RECV: Similar to FASTRET, when waiting for a message-send rendezvous
; When set together with IN_SEND, it's a sendrcv and the SEND needs to finish
; first.
; At any time when IN_RECV is set, the proc's saved rdi contains a pointer to
; the handle being received from.
; When a process starts a receive, until it becomes properly blocked on some
; process or finishes the receive immediately, it will be both RUNNING and
; IN_RECV.
defbit	PROC_IN_RECV,	2
; Process is trying to do a synchronous send or sendrcv, blocking on the
; waiting_for process to reach a PROC_IN_RECV state. Both IN_SEND and IN_RECV
; can be set at the same time.
; At any time when IN_SEND is set, the proc's saved rdi contains a pointer to
; the handle being sent to.
; When a process starts a send, until it becomes properly blocked on some
; process or finishes the operation, it will be both RUNNING and IN_SEND.
defbit	PROC_IN_SEND,	3
; Is the currently running process
defbit	PROC_RUNNING,	4
; Process has had a page fault that requires a response from a backer, or has
; requested a page paged in.
; proc.fault_addr is the address that faulted/was requested.
defbit	PROC_PFAULT,	5
