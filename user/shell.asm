%include "module.inc"

con_handle	equ	3

boot:
	; We have 0x1000 (4k) of stack. Let's allocate 1024 or so for reading
	; a line.
	sub	esp, 0x400
	push	rsp

lodstr	edi,	"Shell v0.1", 10
	call	printf

prompt:
	mov	edi, '$'
	call	putchar
	mov	edi, ' '
	call	putchar

cmd_loop:
	call	getchar
	pop	rdi
	push	rdi

	cmp	al, 0xa
	je	newline

%if 1
	mov	edi, eax
	call	putchar
%else
lodstr	edi,	'Char %x', 10
	mov	edi, eax
	call	printf
%endif

	jmp	cmd_loop

newline:
	mov	edi, 10
	call	putchar

	jmp	prompt

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

%include "printf.inc"
