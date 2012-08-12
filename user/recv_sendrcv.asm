	push	rdi
	mov	rsi, rdi
	mov	eax, SYSCALL_RECV
	syscall
.loop:
	mov	edi, msg_req(MSG_USER)
	mov	rsi, [rsp]
	mov	eax, SYSCALL_SENDRCV
	syscall
	inc	r10
	jmp	.loop
