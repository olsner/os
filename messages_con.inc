; vim:filetype=nasm:

MSG_CON_WRITE	equ	MSG_USER
; Should be sendrcv'd, returns one byte of data (when something arrives)
MSG_CON_READ	equ	MSG_USER + 1
