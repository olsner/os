; vim:ts=8:sts=8:sw=8:filetype=nasm:

extern printf
global main

rdtsc64:
	rdtsc
	shl	rdx, 32
	or	rax, rdx
	ret

print_rax:
	mov	rbx, rax
	mov	rsi, rax
	mov	al, 2
	call	printf
	mov	rax, rbx
	ret

print_tsc:
	lea	rax, [rel print_rax]
	push	rax
	jmp	rdtsc64

%define UNROLL 12

; normal call/ret
bench1:
%rep UNROLL
	call	.test
%endrep
	loop	bench1
	ret
.test:
	ret

bench2:
%assign i 0
%rep UNROLL
	lea	rax, [rel .loop %+ i]
	jmp	.test
.loop %+ i:
%assign i i+1
%endrep
	loop	bench2
	ret
.test:
	jmp	rax

bench3:
%assign i 0
	lea	rax, [rel .loop0]
%rep UNROLL
	jmp	.test
.loop %+ i:
	add	rax, .loop1-.loop0
	;lea	rax, [rax+.loop1-.loop0]
%assign i i+1
%endrep
	loop	bench3
	ret
.test:
	jmp	rax

BENCHCOUNT	equ 1*1000*1000

main:
	mov	rcx, 10

.warmup:
	push	rcx
	call	rdtsc64
	mov	r15, rax

	mov	rcx, BENCHCOUNT
	call	bench1
	
	call	rdtsc64
	mov	r14, rax

	mov	rcx, BENCHCOUNT
	call	bench2

	call	rdtsc64
	mov	r13, rax

	mov	rcx, BENCHCOUNT
	call	bench3

	call	rdtsc64
	mov	r12, rax

	pop	rcx
	loop	.warmup

	; rax - r13 = bench3 = register-return clever
	lea	rdi, [rel .msg1]
	sub	rax, r13
	mov	r12, rax
	call	print_rax

	; r13 - r14 = bench2 = register-return normal (lea for each call)
	lea	rdi, [rel .msg2]
	mov	rax, r13
	sub	rax, r14
	mov	r11, rax
	push	r11
	call	print_rax

	;r14-r15 = bench1 = normal call/ret
	lea	rdi, [rel .msg3]
	mov	rax, r14
	sub	rax, r15
	mov	r10, rax
	push	r10
	call	print_rax

	; r11 - r12: speedup from cleverness (reusing patterns in return address)
	lea	rdi, [rel .cleverspeedup]
	pop	r10
	pop	r11
	push	r10
	mov	rax, r11
	sub	rax, r12
	call	print_rax

	lea	rdi, [rel .regretspeedup]
	pop	r10
	mov	rax, r10
	sub	rax, r11
	call	print_rax

	xor	eax,eax
	ret
.msg1:
	db	'reg. ret clever %ld',10,0
.msg2:
	db	'reg. ret normal %ld',10,0
.msg3:
	db	'call/ret %ld',10,0
.regretspeedup:
	db	'speedup from call/ret to reg-ret: %ld',10,0
.cleverspeedup:
	db	'speedup from reg-ret to clever reg-ret: %ld',10,0
