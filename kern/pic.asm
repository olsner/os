; PIC driver, wraps IRQ driver for PIC-originated interrupts
%include "module.inc"
%include "pic.inc"

; Base vector for PIC interrupts. Assume slave is mapped at base + 8.
PIC_IRQ_BASE	equ	0x20
; Base where we map incoming IRQs.
IN_IRQ_BASE	equ	0x30

boot:
	; Input: IRQ driver in rdi
	push	rdi
	; Check that rdi doesn't overlap our internal stuff:
	; 0x20..0x2f: keep track of our clients
	; 0x30..0x3f: our receivers for interrupts
	; 1: temporary handle for something that is registering itself

	; Reinitialize PIC?
	; * Mask all interrupts (we don't want them until someone registers)
	; * Map either to a constant range of real interrupts, or have some
	; way to "allocate" through the IRQ driver?

	; For now - assume PICs are mapped to 0x20..0x2f and all interrupts are
	; masked (this is what start32.inc does).

	mov	ebx, 16
.reg_loop:
	; Duplicate IRQ handler -> 0x30..0x3f (incoming IRQ)
	mov	rdi, [rsp]
	mov	rsi, rdi
	lea	edx, [rbx + IN_IRQ_BASE - 1]
	mov	eax, MSG_HMOD
	syscall
	; Then register it for IRQ 0x20..0x2f
	lea	edi, [rbx + IN_IRQ_BASE - 1]
	lea	esi, [rbx + PIC_IRQ_BASE - 1]
	mov	eax, msg_call(MSG_REG_IRQ)
	syscall
	dec	ebx
	jnz	.reg_loop

rcv_loop:
	zero	eax
	mov	edi, 1 ; 1 = anonymous new receiver
	syscall

	cmp	rdi, [rsp] ; Incoming raw IRQ
	je	irq

	cmp	eax, MSG_REG_IRQ
	je	reg_irq

	cmp	eax, MSG_IRQ_ACK
	je	ack_irq

	jmp	rcv_loop

reg_irq:
	; rsi = irq number to register
	; Rename incoming handle (1) to the IRQ they registered
	push	rsi
	mov	eax, MSG_HMOD
	zero	edx
	syscall

	lea	edi, [rsi - PIC_IRQ_BASE]
	call	unmask

	; TODO Do whatever is needed for the "raw IRQ" handler to talk to us

	; Send response to tell caller they're registered
	pop	rdi
	mov	eax, msg_send(MSG_REG_IRQ)
	syscall

	jmp	rcv_loop

irq:
	; rdi = IN_IRQ_BASE + num
	sub	edi, IN_IRQ_BASE

	; Mask interrupt (ignore it until the driver has responded back to us)
	in	al, PIC1_DATA
	bts	ax, di
	out	PIC1_DATA, al

	; Since we use the mask to control exactly which IRQs get delivered, we
	; can use non-specific EOI (and we do it right away so that another IRQ
	; can get delivered ASAP).
	mov	al, PIC_EOI
	out	PIC1_CMD, al

	; Unmask later when we get a response from the handler.

	; Since this send is blocking, there's a time here where we are left
	; unable to respond to interrupts. Bad stuff. I think something will
	; be done elsewhere to allow interrupts to be queued.
	add	edi, PIC_IRQ_BASE
	mov	eax, msg_send(MSG_IRQ_T)
	syscall

	jmp	rcv_loop

ack_irq:
	; The EOI was already sent. Now we just need to unmask the interrupt
	; to allow it to be delivered again.

	; TODO Needs some validation :)
	lea	edi, [rdi - 0x20]
	call	unmask
	jmp	rcv_loop

unmask:
	cmp	edi, 8
	jae	unmask_slave

	in	al, PIC1_DATA
	; Unmask registered IRQ
	btr	eax, edi
	out	PIC1_DATA, al

	ret

unmask_slave:
	sub	edi, 8
	in	al, PIC2_DATA
	btr	eax, edi
	out	PIC2_DATA, al
	mov	edi, 2
	jmp	unmask
