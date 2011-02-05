; vim:ts=8:sts=8:sw=8:filetype=nasm:
; This is the program stored in the boot sector, that
; loads the kernel bootstrap (kernelstrap.asm)

bits 16
org 0x0000
zero:
	mov	ax,0x07c0
	mov	ds,ax
	
	xor	ax,ax
	mov	ss,ax
	mov	sp,0xffff
	jmp	start

; This is the data area
magic:	db	'-rom1fs-'
file:	db	'kstart.b'

; bogus, let's just assume sizeof(kernel) < 15*512
KERNEL_SIZE EQU 0x0f

disk_burp:
	xor	cx,cx
	mov	cl,ah
db_loop:
	mov	ax,0x0e00 | 'D'
	mov	bl,0x0f
	int	0x10
	dec	cx
	jnz	db_loop
	
	jmp	burp

magic_burp:
	mov	ax,0x0e4d
	mov	bl,0x0f
	int	0x10
	
	jmp	burp
	
find_burp:
	mov	ax,0x0e46
	mov	bl,0x0f
	int	0x10

burp:
	mov	ax,0x0e00 | 'E'
	mov	bl,0x0f
	int	0x10
	cli
	hlt

start:

	xor	ax,ax
	mov	dl,80h
	int 	13h

; Go through the romfs on partition one on the disk
; We want the file called "kstart.b"
	mov	ax,0x800 ; Just after our boot sector
	mov	es,ax
	xor	bx,bx	; Offset in output buffer
	mov	dx,0x0180	; dh = disk head 1, dl = drive 80h/first hd
	mov	cx,0x0001 ; ch = cylinder, cl = sector
	mov	ah,0x02
	mov	al,0x01
	
	int	13h
	jc	disk_burp
	
	xor	si,si
	mov	cx,8
	mov	ah,0x0e
	mov	bl,0x0f
	mov	al,':'
	int	10h
.mprint:
	mov	al, [es:si]
	int	10h
	inc	si
	dec	cx
	jnz	.mprint
	mov	al,':'
	int	10h
	
	mov	eax, [magic]
	cmp	[es:0], eax
	jne	magic_burp
	mov 	eax, [magic+4]
	cmp	[es:4], eax
	jne	magic_burp
	
	; We have the romfs structure at es:0
	; Take what we need, then replace buffer
	; The first file header should be at es:0
	mov	ebp,[es:8]	; edi is not destroyed by int 13h?
	shr	ebp,9
	jz	find_burp
	inc	bp
	mov	cx,0x0001	; First time, sector 1 at es:0-0x200
	xor	bx,bx		; Offset in output buffer = 0
	mov	dx,0x0180	; dh = disk head 1, dl = drive 80h/first hd

find_loop:
	push	cx

	mov	si,0x1f0
	mov	ah,0x0e
	mov	bl,0x0f
	mov	cx,8
.print:
	mov	al, [es:si]
	int	10h
	inc	si
	dec	cx
	jnz	.print
	
	mov	al,':'
	int	10h
	
	pop	cx

	mov	eax,[es:0x1f0]
	cmp	eax,[file]
	jne	notfound
	
	mov	eax,[es:0x1f4]
	cmp	eax,[file+4]
	je	found
	
notfound:
	mov	ah,0x02
	mov	al,0x01
	inc	cx
	push	bp

	int	13h
	jc	near disk_burp
	
	pop	bp
	
	cmp	cx,0x3f
	jg	near burp
		
	dec	bp
	jnz	find_loop
	
found:
	mov	ebx,[es:0x1e8]
	shr	ebx,9
	mov	ax,0x0200+KERNEL_SIZE
	mov	dx,0x0180
	add	al,bl
	xor	bx,bx
	inc	cx
	
	int	13h
	jc	near disk_burp
	cmp	al,KERNEL_SIZE
	jne	near disk_burp
	
	mov	ax,0x0e00+'!'
	mov	bl,0x0f
	int	10h
	
	push	es
	push	word 0
	retf

exit:
	mov	ax,0x0e00+'.'
	mov	bl,0x0f
	int	10h

;Hang the computer
	cli
	hlt
	
	times 	438-($-$$) db 0 ; Pad to the right location
	times	64+8	db 0 ; 64 bytes for partition table, 
			     ; the extra 8 bytes are corrupted by some
			     ; partitioners
; Must be the two last bytes of a sector 
	dw	0xaa55
