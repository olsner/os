; PIC driver, wraps IRQ driver for PIC-originated interrupts
%include "module.inc"
%include "pic.inc"

; Base vector for PIC interrupts. Assume slave is mapped at base + 8.
; Clients are mapped at PIC_IRQ_BASE..PIC_IRQ_BASE+15
; This is also the same as the raw IRQ numbers we listen to.
; Note: clients register for IRQs 0..15 :)
PIC_IRQ_BASE	equ	0x20
; Base where we map incoming IRQs. (handles to rawIRQ process)
IN_IRQ_BASE	equ	0x30

fresh_handle	equ	2

boot:
	; Input: IRQ driver in rdi
	mov	edi, 1
	push	rdi
	; Check that rdi doesn't overlap our internal stuff:
	; 0x20..0x2f: keep track of our clients
	; 0x30..0x3f: our receivers for interrupts
	; 1: temporary handle for something that is registering itself

lodstr	rdi, 'PIC booting...', 10
	call	printf

	; Reinitialize PIC?
	; * Mask all interrupts (we don't want them until someone registers)
	; * Map either to a constant range of real interrupts, or have some
	; way to "allocate" through the IRQ driver?

	; For now - assume PICs are mapped to 0x20..0x2f and all interrupts are
	; masked (this is what start32.inc does).

	mov	ebx, 2
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

	mov	eax, MSG_HMOD
	mov	edi, fresh_handle
	; delete
	zero	esi
	zero	edx
	syscall

	mov	rsi, [rsp]
lodstr	rdi, 'PIC boot complete. rawIRQ is %x', 10
	call	printf

rcv_loop:
	zero	eax
	mov	edi, fresh_handle
	syscall

	push	rax
	push	rdi
	push	rsi

	mov	rcx, rsi
	mov	rdx, rdi
	mov	rsi, rax
lodstr	rdi, 'PIC received %x from %x: %x', 10
	call	printf

	pop	rsi
	pop	rdi
	pop	rax

	cmp	rdi, [rsp] ; Incoming raw IRQ
	je	irq

	cmp	al, MSG_REG_IRQ
	je	reg_irq

	cmp	eax, MSG_IRQ_ACK
	je	ack_irq

	jmp	rcv_loop

; rsi = 0..15
; rdi = (probably) fresh handle
;
; Remap rdi to rdi + PIC_IRQ_BASE (where we have our clients), then unmask
; the corresponding IRQ.
reg_irq:
	push	rsi
%if 1
	push	rdi
lodstr	rdi,	'PIC registering IRQ %x', 10
	call	printf

	pop	rdi
	mov	rsi, [rsp]
%endif

	; Rename incoming handle to the IRQ they registered + PIC_IRQ_BASE
	add	esi, PIC_IRQ_BASE
	mov	eax, MSG_HMOD
	zero	edx
	syscall

	mov	edi, [rsp]
	call	unmask

	; Send response to tell caller they're registered
	pop	rdi
	add	edi, PIC_IRQ_BASE
	mov	eax, msg_send(MSG_REG_IRQ)
	syscall

	jmp	rcv_loop

irq:
	; rdi = IN_IRQ_BASE + num
	sub	edi, IN_IRQ_BASE

	; TODO Check for slave IRQs and handle them.
	; Also check for spurious IRQs for 7 and 15

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
	mov	esi, edi ; actual irq number
	add	edi, PIC_IRQ_BASE
	mov	eax, msg_send(MSG_IRQ_T)
	syscall

	jmp	rcv_loop

ack_irq:
	; The EOI was already sent. Now we just need to unmask the interrupt
	; to allow it to be delivered again.

	; TODO Needs some validation :)
	sub	edi, PIC_IRQ_BASE
	call	unmask
	jmp	rcv_loop

unmask:
	cmp	edi, 8
	jae	unmask_slave

	;in	al, PIC1_DATA
	; Unmask registered IRQ
	btr	eax, edi
	;out	PIC1_DATA, al

	ret

unmask_slave:
	sub	edi, 8
	;in	al, PIC2_DATA
	btr	eax, edi
	;out	PIC2_DATA, al
	mov	edi, 2
	jmp	unmask

%include "printf.asm"
%include "putchar.asm"
