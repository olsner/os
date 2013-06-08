%include "module.inc"

	movq	xmm1, rbx
	movq	xmm0, xmm1
.start:
	mov	ebx, 7
.loop
	lea	edi,['a'+rbx]
	call	putchar

	paddq	xmm0, xmm1
	dec	ebx
	jnz	.loop

.end:
lodstr	edi,	'Hello World from puts', 10
	call	puts

lodstr	edi,	'printf %% "%s" %c',10,0
lodstr	esi,	'Hello World',0
	mov	edx,'C'
	call	printf

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	.start

%include "printf.asm"
%include "putchar.asm"
