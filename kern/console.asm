%include "module.inc"

%define log 0

; set to 1 to disable AT/XT translation and activate scan code set 2.
; the XT codes are easier to deal with though :)
%define use_set2 0

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
KEY_CMD		equ 0x64

boot:
%if log
lodstr	edi, 'console: booting...', 10
	call	printf
%endif

	; bits:
	; 0 = first port IRQ (1 = enabled)
	; 1 = second port irq (disabled)
	; 2 = passed POST (not sure if need to write, but let's say we did pass
	; POST)
	; 3 = should be zero
	; 4 = first ps/2 port clock (1 = disabled)
	; 5 = second ps/2 port clock (1 = disabled)
	; 6 = first ps/2 port translation (1 = enabled)
	;     we want this disabled, to get the raw scan codes from keyboard
CONFIG_BYTE	equ	1 | 4

%if use_set2
	mov	di, KEY_CMD
	mov	si, 0x60 ; write config byte
	call	outb
	call	wait_ready_for_write
	mov	di, KEY_DATA
	mov	si, CONFIG_BYTE
	call	outb
	call	wait_ready_for_write
	mov	di, KEY_DATA
	mov	si, 0xff ; reset
	call	outb
	call	wait_ready_for_write
	mov	di, KEY_DATA
	mov	si, 0xf0 ; set scan code set
	call	outb
	call	wait_ready_for_write
	mov	di, KEY_DATA
	mov	si, 2 ; scan code set 2
	call	outb
%endif
	call	clear_buffer
	call	clear_buffer

	; TODO Reinitialize the keyboard and 8042 here. We should not assume it
	; has a sane state after the boot loader.
	; Also, the key-up code of the last input to grub often comes in just
	; after boot up. We should ignore that (would come as a bonus after
	; resetting the keyboard.)

	mov	edi, pic_driver
	push	rdi
	push	byte 0
	mov	ebp, esp
	; [rbp]: last character read, or 0
	; [rbp+1]: non-zero if there's a process waiting for a key press, or a
	; process in the middle of writing a line.
	; [rbp+4]: shift state

	mov	esi, IRQ_KEYBOARD
	mov	eax, msg_call(MSG_REG_IRQ)
	syscall

%if log
lodstr	edi, 'console: boot complete', 10
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
	push	rsi
	cmp	esi, 10
	je	.newline

	cmp	rdi, the_reader
	je	.write

	; rename the writer to the_reader
	mov	eax, MSG_HMOD
	mov	esi, the_reader
	zero	edx
	syscall

.write:
	; Let's cheat for now
	; Later: have the frame buffer mapped in this process instead.
	mov	eax, MSG_SYSCALL_WRITE
	pop	rdi ; the character pushed above
	syscall

	jmp	rcv_loop

.newline:
	; new line, clear the reader
	call	clear_reader
	jmp	.write

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
	call	clear_reader

	zero	eax
	mov	[rbp], eax

	ret

clear_reader:
	mov	eax, MSG_HMOD
	mov	edi, the_reader
	zero	esi
	zero	edx
	syscall
	ret

irq_message:
	push	rdi
	; Assume message == MSG_IRQ_T

	mov	edi, KEY_DATA
	call	inb

	mov	[rbp], al
%if log
	mov	esi, eax
	movzx	edx, byte [rbp + 1]
lodstr	edi,	'Key scancode received: %x (waiting=%x)', 10
	call	printf
%endif

	call	map_key

%if log
	movzx	esi, byte [rbp]
	mov	edx, esi
	movzx	ecx, byte [rbp + 1]
lodstr	edi,	'ASCII received: %c (%x) (waiting=%x)', 10
	call	printf
%endif

	; mapping ate the key, or it didn't have a mapping or shouldn't output
	; any characters.
	cmp	byte [rbp], 0
	jz	.ack_and_ret
	cmp	byte [rbp + 1], 0
	jz	.ack_and_ret
	call	have_key
.ack_and_ret:

	pop	rdi
	mov	eax, msg_send(MSG_IRQ_ACK)
	syscall

	jmp	rcv_loop

%if use_set2
wait_ready_for_write:
	mov	di, KEY_CMD
	call	inb
	test	al, 2
	jnz	wait_ready_for_write
	ret
%endif

clear_buffer:
	mov	di, KEY_DATA
	call	inb
	mov	di, KEY_CMD
	call	inb
	test	al, 1
	jnz	clear_buffer
.ret	ret

IS_SHIFTED	equ	1

; one key event: optional e0 (or e1/e2), followed by one make/break code
; for now, we ignore all e0 codes and only look at the "normal" code that follows

; [rbp] = key
; [rbp + 4] = shift state
map_key:
	movzx	eax, byte [rbp]
	cmp	al, 0xe0
	je	.ignore_this

	and	al, 0x7f
	cmp	al, keymap.numkeys
	ja	.ignore_this

.can_map_key:
	test	byte [rbp + 4], IS_SHIFTED
	jz	.not_shifted
	add	eax, keymap.shifted - keymap
.not_shifted:
	mov	al, [keymap + rax]
	cmp	al, SPEC_SHIFT
	je	.shift

.key_mapped:
	; press events don't generate any characters
	test	byte [rbp], 0x80
	jz	.ret
.ignore_this:
	xor	eax,eax
.ret
	; al is ascii key code of a released normal key (or 0 for an ignored
	; one).
	mov	byte [rbp], al
	ret

.shift:
	and	byte [rbp + 4], ~IS_SHIFTED
	test	byte [rbp], 0x80
	jnz	.ignore_this
	or	byte [rbp + 4], IS_SHIFTED
	jmp	.ignore_this

%include "portio.asm"
%if log
%include "printf.asm"
%include "putchar.asm"
%endif
%include "keymap.asm"
