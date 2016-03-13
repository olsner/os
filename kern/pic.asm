; PIC driver, wraps IRQ driver for PIC-originated interrupts
%include "module.inc"
%include "pic.inc"

%define log 0

; Base vector for PIC interrupts. Assume slave is mapped at base + 8.
; Clients are mapped at PIC_IRQ_BASE..PIC_IRQ_BASE+15
; This is also the same as the raw IRQ numbers we listen to.
; Note: clients register for IRQs 0..15 :)
PIC_IRQ_BASE	equ	0x20
; Base where we map incoming IRQs. (handles to rawIRQ process)
IN_IRQ_BASE	equ	0x30

IRQ_DRIVER	equ	1
fresh_handle	equ	0x100

boot:
	; Input: IRQ driver in rdi
	mov	edi, IRQ_DRIVER
	push	rdi
	; Check that rdi doesn't overlap our internal stuff:
	; 0x20..0x2f: keep track of our clients
	; 0x30..0x3f: our receivers for interrupts
	; 1: temporary handle for something that is registering itself

%if log
lodstr	edi, 'PIC booting...', 10
	call	printf
%endif

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

	mov	eax, MSG_HMOD
	mov	edi, fresh_handle
	; delete
	zero	esi
	zero	edx
	syscall

	mov	edi, PIC1_CMD
	mov	esi, PIC_EOI
	call	outb
	mov	edi, PIC2_CMD
	mov	esi, PIC_EOI
	call	outb

%if log
	mov	rsi, [rsp]
lodstr	edi, 'PIC boot complete. rawIRQ is %x', 10
	call	printf
%endif

rcv_loop:
	zero	eax
	mov	edi, fresh_handle
	syscall

	push	rax
	push	rdi
	push	rsi

%if log
	mov	rcx, rsi
	mov	rdx, rdi
	mov	rsi, rax
lodstr	edi, 'PIC received %x from %x: %x', 10
	call	printf
%endif

	pop	rsi
	pop	rdi
	pop	rax

	cmp	edi, IN_IRQ_BASE + 16
	jae	.not_irq
	cmp	edi, IN_IRQ_BASE
	jae	irq

.not_irq:
	cmp	ax, MSG_KIND_CALL | MSG_REG_IRQ
	je	reg_irq

	cmp	al, MSG_IRQ_ACK
	je	ack_irq

%if log
lodstr	edi, 'Message %x not handled', 10
	mov	rsi, rax
	call	printf
%endif

	jmp	rcv_loop

; rsi = 0..15
; rdi = (probably) fresh handle
;
; Remap rdi to rdi + PIC_IRQ_BASE (where we have our clients), then unmask
; the corresponding IRQ.
reg_irq:
	push	rsi
%if log
	push	rdi
	mov	rdx, rdi
lodstr	edi,	'PIC registering IRQ %x to %x', 10
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

%if log
	mov	rsi, [rsp]
lodstr	edi,	'PIC: IRQ %x registered', 10
	call	printf
%endif

	; Send response to tell caller they're registered
%if log
	mov	rdi, [rsp]
%else
	pop	rdi
%endif
	add	edi, PIC_IRQ_BASE
	mov	eax, msg_send(MSG_REG_IRQ)
	syscall

%if log
lodstr	edi,	'PIC: registration acknowledged', 10
	pop	rsi
	call	printf
%endif

	jmp	rcv_loop

irq:
%if log
	push	rdi
	lea	esi, [rdi - IN_IRQ_BASE]
lodstr	edi, 'PIC: IRQ %x triggered', 10
	call	printf
	pop	rdi
%endif

	; rdi = IN_IRQ_BASE + num
	sub	edi, IN_IRQ_BASE
	push	rdi

	cmp	edi, 0x8
	jb	.only_master

	; Slave IRQ. Mask the slave irq and EOI the slave PIC.
	lea	esi, [edi - 8]
	mov	edi, PIC2_DATA
	call	pic_mask

	mov	edi, PIC2_CMD
	mov	esi, PIC_EOI
	call	outb

	jmp	.skip_mask

.only_master:
	; TODO Check for spurious IRQs for 7 (and 15)
	; Mask interrupt (ignore it until the driver has responded back to us)
	mov	esi, edi
	mov	edi, PIC1_DATA
	call	pic_mask

.skip_mask:
	; Since we use the mask to control exactly which IRQs get delivered, we
	; can use non-specific EOI (and we do it right away so that another IRQ
	; can get delivered ASAP).
	mov	edi, PIC1_CMD
	mov	esi, PIC_EOI
	call	outb

	; Unmask later when we get a response from the handler.

	pop	rsi
	lea	edi, [rsi + PIC_IRQ_BASE]
	zero	esi
	inc	esi
	mov	eax, MSG_PULSE
	syscall

	jmp	rcv_loop

ack_irq:
%if log
	push	rdi
	mov	esi, edi
lodstr	edi, 'PIC: IRQ %x acknowledged', 10
	call	printf
	pop	rdi
%endif

	; The EOI was already sent. Now we just need to unmask the interrupt
	; to allow it to be delivered again.

	; TODO Needs some validation :)
	sub	edi, PIC_IRQ_BASE
	call	unmask
	jmp	rcv_loop

unmask:
%if log
	push	rdi
	mov	esi, edi
lodstr	edi, 'PIC: unmasking %x', 10
	call	printf
	pop	rdi
%endif

	cmp	edi, 8
	jae	unmask_slave

.not_slave:
	mov	esi, edi
	mov	edi, PIC1_DATA
	call	pic_unmask

	ret

unmask_slave:
	lea	esi, [rdi - 8]
	mov	edi, PIC2_DATA
	call	pic_unmask
	mov	edi, 2
	jmp	unmask.not_slave

; edi = PIC port
; esi = bit to unmask
pic_unmask:
	push	rdi
	push	rsi
	call	inb
	pop	rsi
	btr	eax, esi
	pop	rdi
	mov	esi, eax
	jmp	outb

; edi = PIC port
; esi = bit to mask
pic_mask:
	push	rdi
	push	rsi
	call	inb
	pop	rsi
	bts	eax, esi
	pop	rdi
	mov	esi, eax
	jmp	outb

%include "portio.inc"
%if log
%include "printf.inc"
%include "putchar.inc"
%endif
