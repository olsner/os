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
	mov	ax,pages.memory_map / 16
	mov	es,ax
	mov	di,memory_map.data - pages.memory_map
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
	mov	dword [es:memory_map.size - pages.memory_map], edi

	mov	ax,0x0e00+'B'
	mov	bl,0x0f
	int	10h

	; Protect Enable -> 1
	mov	eax,cr0
	or	al,CR0_PE
	mov	cr0,eax

	; I think the offsets here are because it's DS-relative and DS is 0x800
	lidt	[idtr - pages.kernel]
	lgdt	[gdtr - pages.kernel]

	mov	ax,data_seg
	mov	ds,ax
	mov	es,ax
	mov	ss,ax

	; Reset cs by far-jumping to the other side
	o32 jmp	code_seg:start32
