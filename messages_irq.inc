; vim:filetype=nasm:

; Register for IRQ
;
; Should be sent as a sendrcv from a fresh handle - this handle will become
; associated with the given IRQ. The IRQ will send MSG_IRQ_T when triggered and
; expects to receive back a MSG_IRQ_ACK when the interrupt is handled.
;
; IRQ messages will be sent asynchronously - if the receivers are not in a
; message receive phase at that time the IRQ will be dropped.
;
; Takes one parameter, the IRQ number to listen for.
MSG_REG_IRQ	equ	MSG_USER + 0

; The IRQ has been triggered. Do something!!1
MSG_IRQ_T	equ	MSG_USER + 1
; The IRQ has been handled, feel free to signal us again when something happens.
MSG_IRQ_ACK	equ	MSG_USER + 2
