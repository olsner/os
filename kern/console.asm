%include "module.inc"

; Interface summary:
; * MSG_CON_WRITE: write to screen
;   (use send - console does not respond)
;   byte to write in esi
; * MSG_CON_READ: input a character
;   (use sendrcv - the console will block until you've received the input)
;   Accepts fresh handles.

; (Internals)
; * MSG_IRQ_T: keyboard interrupt
;   responds as soon as the new key is in the buffer
;   (after 15 characters, start dropping stuff)

the_reader equ 1
pic_driver equ 2

IRQ_KEYBOARD	equ 1

boot:
lodstr	rdi, 'Console booting...', 10
	call	printf

	mov	edi, pic_driver
	push	rdi

	mov	esi, IRQ_KEYBOARD
	mov	eax, msg_call(MSG_REG_IRQ)
	syscall

	mov	rsi, rdi
lodstr	rdi, 'Console boot complete, PIC interrupt %x', 10
	call	printf

rcv_loop:
	mov	eax, MSG_HMOD
	mov	edi, the_reader
	; delete
	zero	esi
	zero	edx
	syscall

	zero	eax
	mov	edi,the_reader ; recipient == any, or "the" reader
	syscall
	; rdi = source
	; rax = message type (MSG_CON_*)

	cmp	rdi, [rsp] ; The sender is the PIC driver
	je	irq_message

	cmp	eax, MSG_CON_WRITE
	jz	msg_write

	cmp	eax, MSG_CON_READ
	jz	msg_read

	; unknown
	jmp	rcv_loop

msg_write:
	; Let's cheat for now
	mov	eax, MSG_SYSCALL_WRITE
	mov	edi, esi
	syscall

	jmp	rcv_loop

msg_read:
	cmp	edi, the_reader
	jne	rcv_loop

	mov	eax, msg_send(MSG_CON_READ)
	; TODO Unqueue character from input, or wait for interrupt
	mov	esi, 'a'
	syscall

	jmp	rcv_loop

irq_message:
	; Assume message == MSG_IRQ_T
KEY_DATA	equ 0x60

	zero	eax
	in	al, KEY_DATA

	mov	eax, msg_send(MSG_IRQ_ACK)
	syscall
	jmp	rcv_loop

%include "printf.asm"
%include "putchar.asm"
