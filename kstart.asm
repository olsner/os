; vim:ts=8:sts=8:sw=8:filetype=nasm:
; This is the real bootstrap of the kernel, and
; it is this part that is loaded by the boot sector (boot.asm)

org 0
bits 16

%macro define_segment 3 ; limit, address, flags and access
	dw	%1 & 0xffff ;seg_limit
	dw	%2 & 0xffff ;addr_00_15
	db	(%2) >> 16 ;addr_16_23
	db	(%3) >> 4 ;access
	db	((%3) << 4) & 0xf0 | ((%1) >> 16) & 0x0f ;flags/limit
	db	(%2) >> 24 ;addr_24_31
%endmacro

; Bit	Field
; 7	Present = 1
; 6..5	Ring == 0
; 4	Descriptor type == 1 (user)
; 3..0	Type = cs: 1010, ds: 0010

SEG_PRESENT	equ	1000_0000_0000b
SEG_USER	equ	0001_0000_0000b

SEG_TYPE_CODE	equ	1000_0000b
SEG_TYPE_DATA	equ	0

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

start:
	mov	ax,cs
	mov	ds,ax

	mov	ax,0x0e00+'A'
	mov	bl,0x0f
	int	10h

	cli
	mov	al,0xff
	out	0xa1, al
	mov	al,0xfb
	out	0x21, al
	
	mov	ax,0x0e00+'B'
	mov	bl,0x0f
	int	10h

	; Protect Enable -> 1
	mov	eax,cr0
	or	eax,1
	mov	cr0,eax
	
	lidt	[idtr]
	lgdt	[gdtr]

	; Reset cs by far-jumping to the other side
	jmp	code_seg:dword start32+0x8000
	
bits 32
start32:
	mov	ax,data_seg
	mov	ds,ax
	mov	es,ax

	mov	ebx, 0xb8000
	mov	edi, ebx

	xor	eax,eax
	mov	ecx,2*80*25/4 ; 125 << 3
	rep stosd

	mov	word [ebx],0x0f00+'P'

	; magic flag time:
	; All entries have the same lower 4 bits (all set to 1 here):
	; - present (bit 0)
	; - Read-only/Writable (bit 1), 1 = Writable
	; - User/Supervisor bit (bit 2), 1 = Accessible from user mode
	; - Page-level writethrough (bit 3)
	; For the final one, we set a couple more flags:
	; - Global (bit 8), along with PGE, the page will remain in TLB after
	; changing the page tables, we promise that the page has the same
	; mapping in all tables.
	; - Page Size (bit 7), this is the final page entry rather than a link
	; to another table. In our case, this makes this a 2MB page since the
	; bit is set already in the third level.

	; Write PML4 (one entry, pointing to one PDP)
	mov	edi, 0xa000 ; base address, where we put the PML4's
	mov	eax, 0xb00f ; 0xb000 is where the PDP starts, 0xf is magic
	stosd

	xor	eax,eax
	mov	ecx, 0x03ff ; number of zero double-words in PML4
	rep stosd

	; Write PDP (one entry, pointing to one PD)
	mov	eax, 0xc00f ; 0xc000 is the start of the PD
	stosd

	xor	eax,eax
	mov	ecx, 0x03ff ; number of zero double-words in PDP
	rep stosd

	; Write PD (one entry, pointing to one PT)
	mov	eax, 0xd00f ; 0xd000 points to the final page table
	stosd
	xor	eax,eax
	mov	ecx, 0x03ff
	rep stosd

	; Write PT at 0xd000, will have a few PTE's first that are not present
	; to catch null pointers. Then at 0x8000 to 0x10000 we'll map pages to
	; the same physical address.
	xor	eax,eax
	mov	ecx,0x0400
	rep stosd
	sub	edi,0x1000-8*8
	; Map 8 pages starting at 0x8000 to the same physical address
	mov	eax, 0x800f ; page #8/0x8000 -> physical 0x8000 (i.e. here)
	mov	ecx, 8
.loop:
	stosd
	add	edi, 4
	add	eax, 0x1000
	loop	.loop

	; Provide an identity mapping for VGA memory
	add	edi, ((0xb8000-0x10000) >> 12) << 3
	add	eax, 0xb8000-0x10000
	stosd

	; Start mode-switching
	mov	eax, 10100000b ; PAE and PGE
	mov	cr4, eax

	mov	edx, 0xa000 ; address of PML4
	mov	cr3, edx

	mov	ecx, 0xc0000080 ; EFER MSR
	rdmsr
	or	eax, 0x100 ; Set LME
	wrmsr

	mov	eax,cr0
	or	eax,0x80000000 ; Enable paging
	mov	cr0,eax

	; Set code segment to L=1,D=0
	xor	eax,eax
	mov	edi,gdt_start+0x8000+8
	mov	ecx,4
	rep stosd

	jmp	code64_seg:start64+0x8000

bits 64
start64:
	mov	ax,data64_seg
	mov	ds,ax

	mov	edi,0xb8004
	; Just do something silly that should fail if we weren't in long mode
	shl	rdi,32
	test	edi,edi
	jnz	not_long
	shr	rdi,32
	; Then proceed to write a message
	mov	esi,message+0x8000
	mov	ecx,2
	rep movsq

	mov	rax,8 ; Random

	jmp	$

not_long:
	ud2

	times 4096-($-$$) db 0
message:
	dq 0x0747074e074f074c, 0x07450744074f074d
gdt_start:
	define_segment 0,0,0
	; 32-bit code/data. Used for running ancient code in compatibility mode. (i.e. nothing)
	define_segment 0xfffff,0,RX_ACCESS | GRANULARITY | SEG_32BIT
	define_segment 0xfffff,0,RW_ACCESS | GRANULARITY | SEG_32BIT
	; 64-bit code/data. Used.
	define_segment 0,0,RX_ACCESS | SEG_64BIT
	define_segment 0,0,RW_ACCESS
gdt_end:

align	4
gdtr:
	dw	gdt_end-gdt_start-1 ; Limit
	dd	gdt_start+0x8000  ; Offset

idtr:
	dw	0
	dd	0
