
; di = port
; => ax = input
inb:
	mov	eax, MSG_SYSCALL_IO
	mov	esi, 0x01
	syscall
	ret

; di = port
; sil = byte to output
outb:
	mov	edx, esi
	mov	esi, 0x11
	mov	eax, MSG_SYSCALL_IO
	syscall
	ret

