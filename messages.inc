; vim:filetype=nasm:

; MESSAGE TYPES! AND CODES!

; Placeholder for e.g. no message received by asend/arecv
MSG_NONE	equ	0
; Map a page of this object (or an identified object in this process?)
; * flags:
;   * request to map or "grant" on page?
;   * read-only/read-write/read-exec
;   * shared or private/CoW
; * virtual address on receiving side (depending on flag)
; * offset inside object
; * length to attempt to map
; * (handle of object to map)
MSG_MAP		equ	1
; Received when a fault happens in a process that has mapped a page from you
; Parameters:
; * page(s) to fill with data. will be zeroed out and mapped by kernel
; * offset inside object
; * length/number of pages
; * (handle (in local address space) of mapped object)
; Status (in rax):
; 0 if successfully filled
; !0 on an error. the faulting process will terminate
MSG_PFAULT	equ	2

; Start of user-mapped message-type range
MSG_USER	equ	16
MSG_MAX		equ	255

; Message codes are a 1-byte message number with some flags. For now, that's
; only an error flag (1 or 0) and a response flag that is 0 for requests, 1 for
; responses to them. What the meaning of request/response actually is, depends
; on the message type and should be documented there.
; TODO Find some nice way to put syscall numbers in this code too.
MSG_MASK_CODE		equ 0xff
MSG_BIT_ERROR		equ 8
MSG_BIT_RESPONSE	equ 9
MSG_FLAG_ERROR		equ (1 << MSG_BIT_ERROR)
MSG_FLAG_RESPONSE	equ (1 << MSG_BIT_RESPONSE)

%define msg_code(msg, error, respflag) \
	(msg | (error << MSG_BIT_ERROR) | (respflag << MSG_BIT_RESPONSE))
%define msg_resperr(msg) msg_code(msg, 1, 1)
%define msg_resp(msg) msg_code(msg, 0, 1)
%define msg_req(msg) msg_code(msg, 0, 0)

