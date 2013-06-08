%include "module.inc"

con_handle	equ	3

boot:
	; We have 0x1000 (4k) of stack. Let's allocate 1024 or so for reading
	; a line.
	sub	esp, 0x400
	push	rsp

lodstr	rdi,	'Shell', 10
	call	printf

cmd_loop:
	mov	edi, '$'
	call	putchar

	call	getchar
	pop	rdi
	push	rdi

	cmp	al, 10
	je	newline

lodstr	edi,	'Char %x', 10
	mov	esi, eax
	call	printf

	jmp	cmd_loop

newline:
	mov	edi, 10
	call	putchar
	jmp	cmd_loop

putchar:
	mov	esi, edi
	mov	edi, con_handle
	mov	eax, msg_send(MSG_CON_WRITE)
	syscall
	ret

getchar:
	mov	edi, con_handle
	mov	eax, msg_call(MSG_CON_READ)
	syscall
	mov	eax, esi
	ret

%include "printf.asm"
