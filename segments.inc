; vim:ts=8:sts=8:sw=8:filetype=nasm:

%macro define_segment 3 ; limit, address, flags and access
	dw	%1 & 0xffff ;seg_limit
	dw	%2 & 0xffff ;addr_00_15
	db	((%2) >> 16) & 0xff ;addr_16_23
	db	(%3) >> 4 ;access
	db	((%3) << 4) & 0xf0 | ((%1) >> 16) & 0x0f ;flags/limit
	db	((%2) >> 24) & 0xff ;addr_24_31
%endmacro

%macro define_tss64 2 ; limit, address
	define_segment %1, (%2) & 0xffffffff, SEG_PRESENT | SEG_SYSTEM | SEG_TYPE_TSS
	dd (%2) >> 32
	dd 0
%endmacro

; Bit	Field
; 7	Present = 1
; 6..5	Ring == 0
; 4	Descriptor type == 1 (user)
; 3..0	Type = cs: 1010, ds: 0010

SEG_PRESENT	equ	1000_0000_0000b
SEG_USER	equ	0001_0000_0000b
SEG_SYSTEM	equ	0000_0000_0000b
SEG_DPL3	equ	0110_0000_0000b

SEG_TYPE_CODE	equ	1000_0000b
SEG_TYPE_DATA	equ	0
SEG_TYPE_TSS	equ	1001_0000b

CODE_SEG_RX	equ	0010_0000b
DATA_SEG_RW	equ	0010_0000b

RX_ACCESS	equ	SEG_PRESENT | SEG_USER | SEG_TYPE_CODE | CODE_SEG_RX
RW_ACCESS	equ	SEG_PRESENT | SEG_USER | SEG_TYPE_DATA | DATA_SEG_RW

; Bits in the flags/limit byte - scaled down by one nibble (the low is the high
; bits of the limit)
; Bit	Field
; 7	Granularity
; 6	Default Operand Size (D/B)
GRANULARITY	equ	1000b
SEG_32BIT	equ	0100b
SEG_64BIT	equ	0010b

code_seg	equ	8
data_seg	equ	16
code64_seg	equ	24
data64_seg	equ	32
tss64_seg	equ	40
user_code_seg	equ	56
; code32_user	equ	56
; data32_user	equ	64
user_cs		equ	user_code_seg+16 | 11b
user_ds		equ	user_cs+8

%macro define_gate64 3 ; code-seg, offset, flags
	dw	(%2) & 0xffff
	dw	%1
	db	0
	db	%3
	dw	((%2) >> 16) & 0xffff
	dd	(%2) >> 32
	dd	0
%endmacro

GATE_PRESENT		equ	1000_0000b
GATE_TYPE_INTERRUPT	equ	0000_1110b
; Among other(?) things, a task gate leaves EFLAGS.IF unchanged when invoking the gate
GATE_TYPE_TASK		equ	0000_1111b

