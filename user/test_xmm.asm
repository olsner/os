%include "module.inc"

	mov	ebx, 2
	movq	xmm1, rbx
	movq	xmm0, xmm1
.loop:
	mov	edi,'2'
	call	putchar

	paddq	xmm0,xmm1

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	.loop

%include "putchar.inc"
