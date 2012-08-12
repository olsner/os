bits 64
section .text

printf:
	; al: number of vector arguments (won't be used...)
	; rdi: format string
	; rsi,rdx,rcx,r8,r9: consecutive arguments

	; reorder the stack a bit so that we have all our parameters in a row
	mov	[rsp-32],rsi
	mov	rsi,[rsp]
	mov	[rsp-40],rsi ; rsp-40 is now the return address!
	mov	[rsp-24],rdx
	mov	[rsp-16],rcx
	mov	[rsp-8],r8
	mov	[rsp],r9
	sub	rsp,40
	; rdi: pointer to parameters
	; rsi: pointer to format string
	mov	rsi,rdi
	lea	rdi,[rsp+8]

	push	r12
	push	r13
	push	r14
	push	r15
	push	rbx

.nextchar:
	lodsb
	test	al,al
	jz	.done
	cmp	al,'%'
	je	.handle_format

.write_al:
	mov	r12,rdi
	mov	r13,rsi
	movzx	edi,al
	call	putchar
	mov	rsi,r13
	mov	rdi,r12
	jmp	.nextchar

.handle_format:
	lodsb
	cmp	al,'c'
	je	.fmt_c
	cmp	al,'s'
	je	.fmt_s
	cmp	al,'p'
	je	.fmt_p
	cmp	al,'x'
	je	.fmt_x
	;cmp	al,'%'
	jmp	.write_al

.fmt_c:
	mov	rax,[rdi]
	add	rdi,8
	jmp	.write_al

.fmt_s:
	; syscall will clobber rsi and rdi but not r12 and r13
	lea	r13,[rdi+8]
	mov	r12,rsi

	lea	rsi, [rel null_str]
	mov	rdi, [rdi]
	test	rdi, rdi
	cmovz	rdi, rsi
	call	puts

	mov	rsi,r12
	mov	rdi,r13
	jmp	.nextchar

.fmt_p:
	cmp	qword [rdi], 0
	; Rely on the special-case for null strings to print (null)
	jz	.fmt_s
.fmt_x:
	lea	r13,[rdi+8]
	mov	r12,rsi
	mov	rbx, [rdi]

	cmp	al, 'x'
	setz	r15b

	mov	edi, '0'
	call	putchar
	mov	edi, 'x'
	call	putchar

	mov	cl, 64

	; cl = highest bit of first hex-digit to print (>= 4)
	; rbx = number to print
.print_digits:
	lea	r14, [rel digits]
.loop:
	sub	cl, 4
	mov	rdi, rbx
	shr	rdi, cl
	and	edi, 0xf
	jnz	.print
	test	cl, cl
	jz	.print
	test	r15b, r15b
	jnz	.next_digit
.print:
	mov	r15b, 0
	mov	dil, byte [r14 + rdi]
	push	rcx
	call	putchar
	pop	rcx
.next_digit:
	test	cl, cl
	jnz	.loop

	mov	rsi,r12
	mov	rdi,r13
	jmp	.nextchar

.done:
	pop	rbx
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	clear_clobbered
	add	rsp,48
	jmp	[rsp-48]

puts:
	; callee-save: rbp, rbx, r12-r15
	mov	rsi,rdi

.loop:
	lodsb
	mov	edi,eax
	test	dil,dil
	jz	.ret

	push	rsi
	call	putchar
	pop	rsi
	jmp	.loop

.ret:
	clear_clobbered
	ret

pushsection .rodata
digits:
	db '0123456789abcdef'
null_str:
	db '(null)', 0
popsection
