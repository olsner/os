putchar:
	mov	eax, SYS_WRITE
	syscall
	ret
