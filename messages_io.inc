; vim:filetype=nasm:

; I/O Port 'in'
;
; Parameters:
; * io port number
; * data size, either 1, 2 or 4
; (Is 64-bit port i/o possible?)
;
; Returns the received data. Use as a call.
MSG_IOPORT_IN	equ	MSG_USER + 0
; I/O port 'out'
;
; * io port number
; * data size: 1, 2, or 4
; * data (1-4 bytes actually used)
MSG_IOPORT_OUT	equ	MSG_USER + 1

