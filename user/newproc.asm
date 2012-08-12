%include "module.inc"

[map all newproc.map]

	mov	edi, user_entry_new
	mov	esi, end_of_module
	mov	eax, SYSCALL_NEWPROC
	syscall

	mov	rbx, rax

lodstr	rdi, 'newproc: %p', 10
	mov	rsi, rax
	call	printf

	mov	rdx, 1
	mov	r10, 2
	mov	r8, 3
	mov	r9, 4

.recv_from_new:
	mov	rsi, rbx
	mov	eax, SYSCALL_SENDRCV
	mov	edi, msg_req(MSG_USER)
	syscall

	push	rdx
	push	r8
	push	r9
	push	r10
	mov	rsi, rdi
	mov	rcx, r10
lodstr	rdi, 'old received %p %x %x %x %x', 10
	call	printf
	pop	r10
	pop	r9
	pop	r8
	pop	rdx

	jmp	.recv_from_new

user_entry_new:
	; Creator pid (not parent, parents don't really exist here...)
	push	rdi

	; TODO Define the initial program state for a process.
	; - parent pid
	; - function parameters in registers?
	; - address space

lodstr	rdi, 'user_entry_new', 10
	call	printf

.loop:
	mov	rsi, [rsp]
	mov	eax, SYSCALL_RECV
	syscall

	mov	rsi, rdi
	mov	rcx, r10
	push	rdx
	push	r8
	push	r9
	push	r10
lodstr	rdi, 'new received %p %x %x %x %x', 10
	call printf

	pop	r10
	pop	r9
	pop	r8
	pop	rdx
	mov	edi, msg_resp(MSG_USER)
	mov	rsi, [rsp]
	mov	eax, SYSCALL_SEND
	inc	edx
	syscall

	jmp	.loop

%include "printf.asm"
%include "putchar.asm"
