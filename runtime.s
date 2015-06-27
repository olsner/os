	.text
	.section	.text.memset,"ax",@progbits
	.globl	memset
	.type	memset,@function
memset:
	# rdi = target, rsi = data, rdx = count
	movl %esi, %eax
	movq %rdx, %rcx # save one byte: assume count < 4GB
	rep stosb
	retq
1:
	.size	memset, 1b - memset

	.section	.text.memcpy,"ax",@progbits
	.globl	memcpy
	.type	memcpy,@function
memcpy:
	# rdi = target, rsi = src, rdx = count
	movq %rdx, %rcx
	rep movsb
	retq
1:
	.size	memcpy, 1b - memcpy
