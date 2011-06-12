; vim:ts=8:sts=8:sw=8:filetype=nasm:

bits 16
start16:
	mov	ax,cs
	mov	ds,ax

	mov	ax,0x0e00+'A'
	mov	bl,0x0f
	int	10h

	cli
	mov	al,0xff
	out	0xa1, al
	;mov	al,0xfb
	out	0x21, al

	; Safe area to put random crap like the e820 address map.
	mov	ax,0x900
	mov	es,ax
	mov	di,memory_map.data - 0x9000
	xor	ebx,ebx
.e820_loop:
	mov	ax,0xe820 ; Function code
	; mov bx, -- ; ebx contains the "Continuation value", returned by last call (or 0 on first iteration)
	mov	cx,20 ; Size of output element
	mov	edx,'PAMS'

	int	15h
	jc	.done
	cmp	di,65536-20
	ja	.done

	cmp	eax,'PAMS'
	jne	.done
	test	ebx,ebx
	jz	.done

	add	di,20
	jmp	.e820_loop
.done:
	mov	dword [es:memory_map.size - 0x9000], edi

	mov	ax,0x0e00+'B'
	mov	bl,0x0f
	int	10h

	; Protect Enable -> 1
	mov	eax,cr0
	or	al,CR0_PE
	mov	cr0,eax
	
	lidt	[idtr - 0x8000]
	lgdt	[gdtr - 0x8000]

	mov	ax,data_seg
	mov	ds,ax
	mov	es,ax
	mov	ss,ax

	; Reset cs by far-jumping to the other side
	jmp	code_seg:dword start32
