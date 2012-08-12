; edi: character to put
putchar:
	mov	eax, SYSCALL_WRITE
	syscall
	clear_clobbered
	ret
