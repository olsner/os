%include "module.inc"

[map all newproc.map]

CHILD_HANDLE	equ	1

	mov	edi, CHILD_HANDLE
	mov	ebx, edi ; save it away for later
	mov	esi, user_entry_new
	mov	edx, end_of_module
	mov	eax, MSG_NEWPROC
	syscall

lodstr	rdi, 'old: calling new proc...', 10
	call	printf

	mov	esi, 1
	mov	edx, 2
	mov	r8, 3
	mov	r9, 4
	mov	r10, 5
	mov	eax, msg_call(MSG_USER)
	mov	edi, ebx
	syscall

lodstr	rdi, 'old: call returned message %x', 10
	mov	rsi, rax
	call	printf

	mov	rdx, 1
	mov	r10, 2
	mov	r8, 3
	mov	r9, 4

.call_new:
lodstr	rdi, 'old calling %p...', 10
	mov	rsi, rbx
	call	printf
	mov	eax, msg_call(MSG_USER)
	mov	rdi, rbx
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

	jmp	.call_new

user_entry_new:
	; Creator pid (not parent, parents don't really exist here...)
	push	rdi

	; TODO Define the initial program state for a process.
	; - parent pid
	; - function parameters in registers?
	; - address space

.loop:
	mov	rsi, [rsp]
lodstr	rdi, 'new receiving from %p...', 10
	call	printf

	mov	rdi, [rsp]
	zero	eax
	syscall

	;    (ax di si dx r8 r9 10) ->
	; di (si dx cx r8 r9 st st)

	push	r10
	push	r9
	mov	r9, r8
	mov	r8, rdx
	mov	rcx, rsi
	mov	rdx, rdi
	mov	rsi, rax
lodstr	rdi, 'new received %x from %p: %x %x %x %x %x', 10
	call printf

	pop	r10
	pop	r9
	mov	rdi, [rsp]
	inc	edx
	mov	eax, msg_send(MSG_USER + 1)
	syscall

	jmp	.loop

%include "printf.asm"
%include "putchar.asm"
