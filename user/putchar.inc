; edi: character to put
putchar:
%if 0
	mov	esi, edi
	mov	rdi, HANDLE_CONSOLE
	mov	eax, msg_send(MSG_CON_WRITE)
%else
	mov	eax, MSG_SYSCALL_WRITE
%endif
	syscall
	clear_clobbered
	ret
