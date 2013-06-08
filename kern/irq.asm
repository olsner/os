%include "module.inc"

%define log 0

; This is the out-of-kernel part of the rawIRQ handling. This is started by the
; kernel as the first process, and handed to the second process in rdi. (At
; this point there are no handles in that process, so the handle key can be
; anything.)

; Interface:
; MSG_REG_IRQ: Register the sender for an IRQ (irq number in rsi)
; MSG_IRQ_ACK: Acknowledge an IRQ as received
; (currently ignored because the "raw" irq handler doesn't do EOI handling etc.)
;
; Sends:
; MSG_IRQ_T: to a handle registered for an IRQ

; Assume we only have interrupts 0..255 (actually 32..255 since the first 32
; are reserved for cpu exceptions)
fresh_handle	equ	256
; Further limit the interrupts we actually support
%define MAX_IRQ 0x30
%define IRQ_START 0x20
%define NUM_IRQS (MAX_IRQ - IRQ_START)

boot:
	; no parameters here

	; Allocate space for bits for which interrupts have listeners
	zero	eax
%define NUM_IRQ_WORDS (NUM_IRQS + 63) / 64
; The stack instructions are ridiculously cheap! Storing the count and doing
; the loop takes 6 bytes, push rax takes 1 byte...
%if NUM_IRQ_WORDS > 6
	lea	ecx, [rax + NUM_IRQ_WORDS]
.pushloop:
	push	rax
	loop	.pushloop
%else
	times NUM_IRQ_WORDS push rax
%endif

%if log
lodstr	rdi,	'rawIRQ: boot complete.', 10
	call	puts
%endif

rcv_loop:
	zero	eax
	mov	edi, fresh_handle
	syscall

	cmp	al, MSG_REG_IRQ
	je	reg_irq

	cmp	al, MSG_IRQ_T
	jne	rcv_loop

irq:
	; received interrupt
	; rdi = null (magic message from kernel)
	; rsi = interrupt number
	; (rdx = number of times interrupt was triggered before we responded)

	bt	dword [rsp], esi
	jnc	rcv_loop

%if log
	push	rsi
lodstr	rdi,	'rawIRQ: %x triggered', 10
	call	printf

	pop	rdi
%else
	mov	edi, esi
%endif
	mov	eax, msg_send(MSG_IRQ_T)
	syscall

	jmp	rcv_loop

; rdi is the fresh handle that wants to register
; rsi is the interrupt number
; TODO: support multiple listeners for one interrupt, make a list of them,
; allocate handles dynamically.
reg_irq:
	; Set the flag that says we have a listener for that interrupt.
	; (This is to make sure we know whether to try to forward an incoming
	; interrupt or not.)
	bts	dword [rsp], esi

	; Remap the fresh handle to the interrupt number
	push	rsi
	zero	edx
	mov	eax, MSG_HMOD
	syscall

%if log
lodstr	rdi, 'rawIRQ: %x registered', 10
	mov	rsi, [rsp]
	call	printf
%endif

	pop	rdi
	mov	rsi, rdi
	mov	eax, msg_send(MSG_REG_IRQ)
	syscall

	jmp	rcv_loop

%if log
%include "printf.asm"
%include "putchar.asm"
%endif
