; vim:filetype=nasm:

PIC1_CMD	equ	0x20
PIC1_DATA	equ	0x21
PIC2_CMD	equ	0xa0
PIC2_DATA	equ	0xa1

ICW1_ICW4	equ	0x01
ICW1_INIT	equ	0x10

; 4 = bit 2 set = there's a slave on 2
ICW3_MASTER	equ	4
; 2 = slave has slave ID 2
ICW3_SLAVE	equ	2

ICW4_8086	equ	0x01

; Send this on the command port to signal EOI
PIC_EOI		equ	0x20
; Send this + IRQ to do a specific EOI for some interrupt
PIC_SEOI	equ	0x60
