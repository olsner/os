; vim:ts=8:sts=8:sw=8:filetype=nasm:
; This is the real bootstrap of the kernel, and
; it is this part that is loaded by the boot sector (boot.asm)

org 0x8000
bits 16

%macro define_segment 3 ; limit, address, flags and access
	dw	%1 & 0xffff ;seg_limit
	dw	%2 & 0xffff ;addr_00_15
	db	(%2) >> 16 ;addr_16_23
	db	(%3) >> 4 ;access
	db	((%3) << 4) & 0xf0 | ((%1) >> 16) & 0x0f ;flags/limit
	db	(%2) >> 24 ;addr_24_31
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

start:
	mov	ax,cs
	mov	ds,ax

	mov	ax,0x0e00+'A'
	mov	bl,0x0f
	int	10h

	cli
	mov	al,0xff
	out	0xa1, al
	;mov	al,0xfb
	out	0x21, al
	
	mov	ax,0x0e00+'B'
	mov	bl,0x0f
	int	10h

	; Protect Enable -> 1
	mov	eax,cr0
	or	eax,1
	mov	cr0,eax
	
	lidt	[idtr - 0x8000]
	lgdt	[gdtr - 0x8000]

	; Reset cs by far-jumping to the other side
	jmp	code_seg:dword start32
	
bits 32
start32:
	mov	ax,data_seg
	mov	ds,ax
	mov	es,ax
	mov	ss,ax

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
	; Map 8^H16 pages starting at 0x8000 to the same physical address
	mov	eax, 0x800f ; page #8/0x8000 -> physical 0x8000 (i.e. here)
	mov	ecx, 16
.loop:
	stosd
	add	edi, 4
	add	eax, 0x1000
	loop	.loop

	; Provide an identity mapping for VGA memory
	add	edi, ((0xb8000-0x18000) >> 12) << 3
	add	eax, 0xb8000-0x18000
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

	jmp	code64_seg:start64

bits 64
default rel

start64:
	mov	ax,data64_seg
	mov	ds,ax
	mov	ss,ax

	mov	edi,0xb8004
	; Just do something silly that should fail if we weren't in long mode
	shl	rdi,32
	test	edi,edi
	jnz	not_long
	shr	rdi,32
	; Then proceed to write a message
	mov	esi,message
	mov	ecx,2
	rep movsq

	mov	ax,tss64_seg
	ltr	ax
	mov	rsp,0x11000

	; Set page 0xe000 to uncacheable - this is where we'll map the APIC
	or	byte [0xd000+0xe*8], 0x10

	mov	ecx,0x1b ; APIC_BASE
	rdmsr
	; Clear high part of base address
	xor	edx,edx
	; Set base address to 0xe000, enable (0x800), and set boot-strap CPU
	mov	eax,0xe900
	wrmsr

	or	dword [0xe0f0],0x100 ; APIC Software Enable

	mov	dword [0xe380],10000 ; APICTIC
	;mov	dword [0xe390],10000000 ; APICTCC
	mov	dword [0xe3e0],1010b  ; Divide by 128
	mov	eax,dword [0xe320] ; Timer LVT
	; Clear bit 16 (Mask) to enable timer interrupts
	btr	eax, 16
	; Set bit 17 (Timer Mode) to enable periodic timer
	bts	eax, 17
	mov	al, 32
	mov	dword [0xe320],eax

	xor	eax,eax
	mov	ecx,0c000_0081h
	; cs for syscall (high word) and sysret (low word).
	; cs is loaded from selector or selector+16 depending on whether we're returning to compat (+16) or long mode (+0)
	; ss is loaded from cs+8 (where cs is the cs selector chosen above)
	mov	edx,((user_code_seg | 11b) << 16) | code64_seg
	wrmsr

	inc	ecx ; c000_0082h - LSTAR
	mov	eax,syscall_entry
	cdq
	wrmsr

	inc	ecx ; c000_0083h - CSTAR
	mov	eax,syscall_entry ;_compat
	cdq
	wrmsr

	mov	ecx, 0xc0000080 ; EFER MSR
	rdmsr
	bts	eax, 0 ; Set SCE
	wrmsr

	;ud2
	sti
.loop:
	; esi == counter, edi == vga memory, just after the "long mode" we just printed
	mov	bl,byte [esi]
	mov	byte [rdi],bl
	mov	byte [rdi+1],0x07
	jmp	.loop

not_long:
	jmp	$

handler_err:
	add	rsp,8
handler_no_err:
	push	rax
	inc	byte [counter]
	mov	eax, dword [0xe390]
	mov	dword [counter+4], eax
	mov	dword [0xe380],10000 ; APICTI
	mov	dword [0xe0b0],0 ; EndOfInterrupt
	pop	rax
	iretq

syscall_entry_compat:
	ud2
	db 'COMPAT'

syscall_entry:
	; r11 = old rflags
	; rcx = old rip

	; TODO Should use SwapGS and set up a kernel stack.
	push	rbx
	push	rdi
	mov	edi, 0xb8000
	mov	word [edi+32], 'S'+0x0f00
	movzx	ebx, al ; syscall number, low byte
	and	bl, 0x0f
	cmp	bl, 0x0a
	jb	.skip
	add	bl, 'A'-0x30
.skip:
	add	bx, 0x0f30
	mov	word [edi+34], bx
	pop	rdi
	pop	rbx
	o64 sysret

	times 4096-($-$$) db 0
__DATA__:

message:
	dq 0x0747074e074f074c, 0x07450744074f074d

counter:
	dq 0

tss:
	dd 0 ; Reserved
	dq 0x10000
	dq 0
	dq 0
	times 0x66-28 db 0
	; IOPB starts just after the TSS
	dw	0x68

align 8
gdt_start:
	define_segment 0,0,0
	; KERNEL segments
	; 32-bit code/data. Used for running ancient code in compatibility mode. (i.e. nothing)
	define_segment 0xfffff,0,RX_ACCESS | GRANULARITY | SEG_32BIT
	define_segment 0xfffff,0,RW_ACCESS | GRANULARITY | SEG_32BIT
	; 64-bit code/data. Used.
	define_segment 0,0,RX_ACCESS | SEG_64BIT
	define_segment 0,0,RW_ACCESS
	; 64-bit TSS
	define_tss64 0x68, (0x8000+tss-$$)
	; USER segments
	; 32-bit code/data. Used for running ancient code in compatibility mode. (i.e. nothing)
	define_segment 0xfffff,0,RX_ACCESS | GRANULARITY | SEG_32BIT | SEG_DPL3
	define_segment 0xfffff,0,RW_ACCESS | GRANULARITY | SEG_32BIT | SEG_DPL3
	; 64-bit code/data. Used.
	define_segment 0,0,RX_ACCESS | SEG_64BIT | SEG_DPL3
	define_segment 0,0,RW_ACCESS | SEG_DPL3
gdt_end:

align	4
gdtr:
	dw	gdt_end-gdt_start-1 ; Limit
	dd	gdt_start  ; Offset

idt:
%define default_error \
	define_gate64 code64_seg,0x8000+handler_err-$$,GATE_PRESENT|GATE_TYPE_INTERRUPT
%define default_no_error \
	define_gate64 code64_seg,0x8000+handler_no_err-$$,GATE_PRESENT|GATE_TYPE_INTERRUPT
%define null_gate \
	define_gate64 0,0,GATE_TYPE_INTERRUPT

	; exceptions with errors:
	; - 8/#DF/double fault (always zero)
	; - 10/#TS/Invalid-TSS
	; - 11/#NP/Segment-Not-Present
	; - 12/#SS/Stack Exception
	; - 13/#GP/General Protection
	; - 14/#PF/Page Fault
	; - 17/#AC/Alignment Check

	; 0-7
	%rep 8
	default_no_error
	%endrep
	default_error ; 8
	default_no_error ; 9
	%rep 5
	default_error ; 10-14
	%endrep
	default_no_error ; 15
	default_no_error
	default_error
	%rep (32-18)
	null_gate
	%endrep
	default_no_error ; APIC Timer
idt_end:

idtr:
	dw	idt_end-idt-1
	dd	idt

