%include "module.inc"

	mov	ebx, 2
	movq	xmm1, rbx
	movq	xmm0, xmm1
.loop:
	mov	edi,'2'
	call	putchar

	paddq	xmm0,xmm1

	mov	eax, SYS_YIELD
	syscall

	jmp	.loop

%include "putchar.inc"
