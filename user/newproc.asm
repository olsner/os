%include "module.inc"

CHILD_HANDLE	equ	1

	mov	edi, CHILD_HANDLE
	mov	ebx, edi ; save it away for later
	mov	esi, user_entry_new
	mov	edx, NEWPROC_PROC
	mov	r8d, end_of_module
	mov	eax, MSG_NEWPROC
	syscall

lodstr	edi, 'old: calling new proc...', 10
	call	printf

	mov	esi, 1
	mov	edx, 2
	mov	r8, 3
	mov	r9, 4
	mov	r10, 5
	mov	eax, msg_call(MSG_USER)
	mov	edi, ebx
	syscall

lodstr	edi, 'old: call returned %x from %x: %x %x %x %x %x', 10
	call log_message

lodstr	edi, 'old calling %p...', 10
	mov	rsi, rbx
	call	printf

	mov	esi, 1
	mov	edx, 2
	mov	r8, 3
	mov	r9, 4
	mov	r10, 5

.call_new:
	mov	eax, msg_call(MSG_USER)
	mov	rdi, rbx
	syscall

	test	si, si
	jnz	.call_new
lodstr	edi, 'old received %p %x %x %x %x', 10
	call	log_message

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
;lodstr	edi, 'new receiving from %p...', 10
;	call	printf

	mov	rdi, [rsp]
	zero	eax
	syscall

	;    (ax di si dx r8 r9 10) ->
	; di (si dx cx r8 r9 st st)

;lodstr	edi, 'new received %x from %x: %x %x %x %x %x', 10
;	call log_message

	mov	rdi, [rsp]
	inc	rsi
	mov	eax, msg_send(MSG_USER + 1)
	syscall

	jmp	.loop

; rdi = log message
; [rsp+8] = old rdi
; other registers: as they were when received
; returns: all message registers except rdi restored
log_message:
	push	r8
	push	rdx
	push	rsi
	push	rax
	push	r10
	push	r9
	mov	r9, r8
	mov	r8, rdx
	mov	rcx, rsi
	mov	rdx, [rsp + 7*8]
	mov	rsi, rax
	call printf
	pop	r9
	pop	r10
	pop	rax
	pop	rsi
	pop	rdx
	pop	r8
	ret

%include "printf.inc"
%include "putchar.inc"
