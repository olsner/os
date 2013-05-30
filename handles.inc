; vim:filetype=nasm:

; For now, we waste one whole page per handle (I think)
handle_top	equ	0x7fff_ffff_0000
; FIXME is handle_top the first handle we allocate or just above that?
%define special_handle(n) (handle_top + 4096 * n)
; TODO Let these pass through the actual handle allocation system, and make a
; convention to send them as parameters to process entry points.
HANDLE_IOPORT	equ	special_handle(0)
HANDLE_PIC_IRQ	equ	special_handle(1)
HANDLE_CONSOLE	equ	special_handle(2)

; clock, halting, yielding, misc. stuff.
HANDLE_MISC	equ	special_handle(3)
