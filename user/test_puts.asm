	movq	xmm1, rbx
	movq	xmm0, xmm1
.start:
	mov	ebx, 7
.loop
	lea	edi,['a'+rbx]
	xor	eax,eax
	syscall

	paddq	xmm0, xmm1
	dec	ebx
	jnz	.loop

.end:
lodstr	rdi,	'Hello World from puts', 10
	call	puts

lodstr	rdi,	'printf %% "%s" %c',10,0
lodstr	rsi,	'Hello World',0
	mov	edx,'C'
	call	printf

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	.start

