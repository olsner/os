%include "module.inc"

%define log 0

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
KEY_DATA	equ 0x60

boot:
%if log
lodstr	rdi, 'console: booting...', 10
	call	printf
%endif

	; TODO Reinitialize the keyboard and 8042 here. We should not assume it
	; has a sane state after the boot loader.
	; Also, the key-up code of the last input to grub often comes in just
	; after boot up. We should ignore that (would come as a bonus after
	; resetting the keyboard.)

	mov	edi, pic_driver
	push	rdi
	push	byte 0
	mov	ebp, esp
	; [rsp]: last character read, or 0
	; [rsp+1]: non-zero if there's a process waiting for a key press

	mov	esi, IRQ_KEYBOARD
	mov	eax, msg_call(MSG_REG_IRQ)
	syscall

%if log
lodstr	rdi, 'console: boot complete', 10
	call	printf
%endif

	mov	eax, MSG_HMOD
	mov	edi, the_reader
	; delete
	zero	esi
	zero	edx
	syscall

rcv_loop:
	zero	eax
	zero	edi
	cmp	byte [rsp + 1], 0
	jnz	.waiting
	mov	edi,the_reader ; recipient == any, or "the" reader
.waiting:
	syscall
	; rdi = source
	; rax = message type (MSG_CON_*)

	cmp	rdi, [rsp + 8] ; The sender is the PIC driver
	je	irq_message

	cmp	al, MSG_CON_WRITE
	jz	msg_write

	cmp	al, MSG_CON_READ
	jz	msg_read

	; unknown
	jmp	rcv_loop

msg_write:
	; Let's cheat for now
	; Later: have the frame buffer mapped in this process instead.
	mov	eax, MSG_SYSCALL_WRITE
	mov	edi, esi
	syscall

	jmp	rcv_loop

msg_read:
	push	rdi
%if log
	mov	rsi, rdi
lodstr	edi,	'msg_read from %x', 10
	call	printf
%endif

	pop	rdi
	mov	eax, MSG_HMOD
	mov	esi, the_reader
	zero	edx
	syscall

	mov	byte [rbp + 1], 1
%if log
lodstr	edi,	'Have reader, key=%x waiting=%x', 10
	movzx	esi, byte [rbp]
	movzx	edx, byte [rbp + 1]
	call	printf
%endif

	; no character queued yet, wait for input
	cmp	byte [rbp], 0
	je	rcv_loop
	call	have_key
	jmp	rcv_loop

have_key:
%if log
lodstr	edi,	'Have key %x (waiting=%x), sending to reader', 10
	movzx	esi, byte [rbp]
	movzx	edx, byte [rbp + 1]
	call	printf
%endif

	mov	edi, the_reader
	mov	eax, msg_send(MSG_CON_READ)
	movzx	esi, byte [rbp]
	syscall

	; delete the reader now that it's done
	mov	eax, MSG_HMOD
	mov	edi, the_reader
	zero	esi
	zero	edx
	syscall

	zero	eax
	mov	[rbp], eax

	ret

irq_message:
	push	rdi
	; Assume message == MSG_IRQ_T

	mov	edi, KEY_DATA
	call	inb

	test	al, 0x80
	jz	.ack_and_ret

	and	al, 0x7f
	add	al, 0x20
	mov	[rbp], al
%if log
	mov	esi, eax
	movzx	edx, byte [rbp + 1]
lodstr	edi,	'Key scancode received: %x (waiting=%x)', 10
	call	printf
%endif

	cmp	byte [rbp + 1], 0
	jz	.ack_and_ret
	call	have_key
.ack_and_ret:

	pop	rdi
	mov	eax, msg_send(MSG_IRQ_ACK)
	syscall

	jmp	rcv_loop

%include "portio.asm"
%if log
%include "printf.asm"
%include "putchar.asm"
%endif
