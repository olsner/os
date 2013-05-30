%include "module.inc"

	; TODO Needs to be updated for new syscall ABI.
	; Writes:
	; * eax = msg_send(MSG_CON_WRITE)
	; * rdi = HANDLE_CONSOLE
	; * esi = char
	; (Or use putchar.)
	; Gettime:
	; * eax = msg_sendrcv(MSG_MISC_GETTIME)
	; * rdi = HANDLE_MISC
	; => rdi = current time
	; Yield:
	; * no-op message to misc process.

	xor	eax,eax

	mov	edi,'U'
	syscall
	mov	edi,10
	syscall
	mov	edi,'V'
	syscall
	mov	edi,10
	syscall

.loop:
	xor	eax,eax
	mov	edi,'|'
	syscall

	;xor	eax,eax
	;mov	edi,'Y'
	;syscall
	;mov	eax,SYSCALL_YIELD
	;syscall

	mov	eax,SYSCALL_GETTIME
	syscall
	movzx	edi,al
	xor	eax,eax
	syscall

	mov	edi,10
	syscall

	mov	eax,SYSCALL_GETTIME
	syscall
	mov	ebp,eax
.notchanged:
	mov	eax,SYSCALL_GETTIME
	syscall
	cmp	al,bpl
	jne	.loop

	jmp	.notchanged

