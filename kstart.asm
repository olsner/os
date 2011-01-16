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
user_cs		equ	user_code_seg | 11b
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

struc	proc
	.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15

	; Aliases for offsets into regs
	.rax	equ	.regs+(0*8)
	.rsp	equ	.regs+(4*8)
	.rsi	equ	.regs+(6*8)

	.rip	resq 1
	.endregs equ .rip
	.rflags	resq 1
	.flags	resq 1 ; See PROC_*
	.waiting_for resq 1 ; Pointer to proc
	.next	resq 1 ; If/when in a list, points to next process in list
	.cr3	resq 1
endstruc

; Jump to CPL 0 instead of CPL 3
PROC_KERNEL	equ	0
; Return to user-mode with sysret, only some registers will be restored:
; rsp, rip: restored to previous values
; rcx, r11: rip and rflags, respectively
; rax: syscall return value
; Remaining registers will be 0 (?)
PROC_FASTRET	equ	1

RFLAGS_IF	equ	(1 << 9)

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

	; Static page allocations:
	; This code (plus data) is at 0x8000-0x9fff (two pages)
	; page tables are written in 0xa000-0xdfff
	; APIC MMIO mapped at 0xe000-0xefff
	; Kernel stack at 0xf000-0xffff (i.e. 0x10000 and growing downwards)
	; User-mode stack at 0x10000-0x10fff
	; Another page-table at 0x11000-0x11fff (for the top-of-vm kernel pages)
	; Kernel GS-page at 0x12000-0x12fff

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
	mov	eax, 0xb005 ; 0xb000 is where the PDP starts
	stosd

	xor	eax,eax
	mov	ecx, 0x03ff ; number of zero double-words in PML4
	rep stosd

	; In 0x11000 we have another PDP. It's global and *not* user-accessible.
	mov	dword [edi-8], 0x11003

	; Write PDP (one entry, pointing to one PD)
	mov	eax, 0xc005 ; 0xc000 is the start of the PD
	stosd

	xor	eax,eax
	mov	ecx, 0x03ff ; number of zero double-words in PDP
	rep stosd

	; Write PD (one entry, pointing to one PT)
	mov	eax, 0xd005 ; 0xd000 points to the final page table
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
	mov	eax, 0x8005 ; page #8/0x8000 -> physical 0x8000 (i.e. here)
	stosd
	; Disable user-mode access to remaining pages, enable write access
	xor	al, 6
	mov	ecx, 15
.loop:
	add	edi, 4
	add	eax, 0x1000
	stosd
	loop	.loop

	; Provide an identity mapping for VGA memory
	add	di, (((0xb8000-0x18000) >> 12) << 3) + 4
	add	eax, 0x1000+0xb8000-0x18000
	stosd

	; Page mapping for kernel space (top 4TB part)
	mov	edi,0x11000
	xor	eax,eax
	mov	ecx,0x0400
	rep	stosd
	mov	word [edi-8],0x1c3

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

kernel_base equ -(1 << 30)

start64:
	lea	rax,[rel kernel_base+.moved]
	jmp	rax
.moved:
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
	mov	esp,0x11000

	; Set page 0xe000 to uncacheable - this is where we'll map the APIC
	or	byte [0xd000+0xe*8], 0x10

	mov	ecx,0x1b ; APIC_BASE
	rdmsr
	; Clear high part of base address
	xor	edx,edx
	; Set base address to 0xe000, enable (0x800), and set boot-strap CPU
	mov	eax,0xe900
	wrmsr

	mov	ebp,0xe000
	mov	ax,0x100
	or	dword [rbp+0xf0],eax ; APIC Software Enable

	mov	ax,10000
	add	bp,0x380
	mov	dword [rbp+0x380-0x380],eax ; APICTIC
	mov	ax,1010b
	mov	dword [rbp+0x3e0-0x380],eax  ; Divide by 128
	mov	eax,dword [rbp+0x320-0x380] ; Timer LVT
	; Clear bit 16 (Mask) to enable timer interrupts
	btr	eax, 16
	; Set bit 17 (Timer Mode) to enable periodic timer
	bts	eax, 17
	mov	al, 32
	mov	dword [rbp+0x320-0x380],eax

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

	inc	ecx ; x000_0084h - SF_MASK
	mov	eax, (1 << 9) | (1 << 17)
	cdq
	wrmsr

	mov	ecx, 0xc0000080 ; EFER MSR
	rdmsr
	bts	eax, 0 ; Set SCE
	wrmsr

	; This is the kernel GS, at 0x12000 (the top of the kernel stack)
	xor	edx,edx
	mov	eax,0x12000
	mov	ecx,0xc000_0101 ; GSBase
	wrmsr

	; after this, ebx should be address to video memory and edi points to
	; the gs-segment data block
	mov	edi,eax
	mov	rax,rsp
	stosq ; gs:0 - user-mode stack seg ; TODO current process...
	mov	eax,0xb8000
	stosq ; gs:8 - VGA buffer base
	lea	eax,[eax+32]
	stosq ; gs:16 - VGA writing position
	lea	eax,[eax+80*25*2-32]
	stosq ; gs:24 - VGA buffer end
	xor	eax,eax
	stosq ; gs:32 - current time
	stosq ; gs:40 - current process
	stosq ; gs:48 - runqueue
	stosq ; gs:56 - idle queue

	lea	rax,[rel user_proc_1]
	jmp	switch_to

user_proc_1:
istruc proc
	at proc.rsp, dq 0x11000
	at proc.rip, dq user_entry
	at proc.rflags, dq RFLAGS_IF
	;at proc.flags, dq (1 << PROC_FASTRET)
	at proc.cr3, dq 0xa000
iend
user_proc_2:
istruc proc
	at proc.rsp, dq 0 ; Doesn't use stack, but that should be fixed...
	at proc.rip, dq user_entry_2
	at proc.rflags, dq RFLAGS_IF
	at proc.flags, dq (1 << PROC_FASTRET)
	at proc.cr3, dq 0xa000
iend

; note to self:
; callee-save: rbp, rbx, r12-r15
; caller-save: rax, rcx, rdx, rsi, rdi, r8-r11
; rsp must be restored when returning (obviously...)

; return value: rax, rdx (or by adding out-parameter)

; arguments: rdi, rsi, rdx, rcx (r10 in syscall), r8, r9

; arg0/rdi = process-struct pointer
; arg1/rsi = entry-point
; returns process-struct pointer in rax
; all "saved" registers are set to 0, except rip and rflags - use other
; functions to e.g. set the address space and link the proc. into the runqueue
init_proc:
	mov	rdx,rcx
	; rdi = proc
	; rdx = entry-point
	mov	cl,proc_size / 8
	movzx	rcx,cl
	xor	rax,rax
	rep stosq
	mov	[rdi-proc_size+proc.rip],rdx
	; bit 1 of byte 1 of rflags: the IF bit
	; all other bits are set to 0 by default
	mov	byte [rdi-proc_size+proc.rflags+1], RFLAGS_IF >> 8
	lea	rax, [rdi-proc_size]
	ret

; Takes process-pointer in rax, never "returns" to the caller (just jmp to it)
switch_to:
	cli	; I don't dare running this thing with interrupts enabled.

	; Make sure we don't invalidate the TLB if we don't have to.
	mov	rcx, [rax+proc.cr3]
	mov	rbx, cr3
	cmp	rbx, rcx
	je	.no_set_cr3
	mov	cr3, rcx
.no_set_cr3:

	mov	rbx, [rax+proc.flags]
	bt	rbx, PROC_KERNEL
	jnc	.user_exit

	; Exit to kernel thread
	; If we don't need to switch rsp this should be easier - restore all
	; regs, rflags, push new cs and do a near return
	push	qword data64_seg
	push	qword [rax+proc.rsp]
	push	qword [rax+proc.rflags]
	push	qword code64_seg
	jmp	.restore_and_iretq

.user_exit:
	; If we stop disabling interrupts above, this will be wildly unsafe.
	; For now, we rely on the flags-restoring part below to atomically
	; restore flags and go to user mode. The risk is if we switch "from
	; kernel" while having the user GS loaded!
	swapgs
	bt	rbx, PROC_FASTRET
	jc	.fast_ret

; push cs before this
	; Push stuff for iretq
	push	qword user_ds
	push	qword [rax+proc.rsp]
	push	qword [rax+proc.rflags]
	push	qword user_cs
.restore_and_iretq:
	push	qword [rax+proc.rip]
	; rax is first, but we'll take it last...
	lea	rsi, [rax+proc.rax+8]

%macro lodregs 1-*
	%rep %0
	%ifidni %1,skip
	lea	rsi,[rsi+4]
	%elifidni %1,rsi
	%error rsi is in use by this macro
	%elifidni %1,rax
	%error rax is in use by this macro
	%else
	lodsq
	mov	%1,rax
	%endif
	%rotate 1
	%endrep
%endmacro

	;.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15
	; skip rsp (iret will fix that) and rsi (we're still using it for the lodsqs, for now)
	lodregs	rcx,rdx,rbx,SKIP,rbp,SKIP,rdi,r8,r9,r10,r11,r12,r13,r14,r15
	mov	rax, [rsi-proc.endregs+proc.rax]
	mov	rsi, [rsi-proc.endregs+proc.rsi]
	iretq

.fast_ret:
	mov	rsp, [rax+proc.rsp]
	mov	rcx, [rax+proc.rip]
	mov	r11, [rax+proc.rflags]
	mov	rax, [rax+proc.rax]
	o64 sysret

; End switch

user_entry:
	xor	eax,eax

	mov	bl,'U'
	movzx	ebx,bl
	syscall

	mov	bl,10
	movzx	ebx,bl
	syscall

	mov	bl,'V'
	movzx	ebx,bl
	syscall

	mov	bl,10
	movzx	ebx,bl
	syscall

.loop:
	mov	al,1
	movzx	eax,al
	syscall
	movzx	ebx,al
	xor	eax,eax
	syscall

	mov	bl,10
	movzx	ebx,bl
	syscall

	mov	al,1
	movzx	eax,al
	syscall
	mov	edx,eax
.notchanged:
	;hlt
	mov	al,1
	movzx	eax,al
	syscall
	cmp	al,dl
	jne	.loop
	jmp	.notchanged

user_entry_2:
	mov	bl,'2'
	xor	eax,eax
	syscall
	jmp	user_entry_2

not_long:
	jmp	$

handler_err:
	add	rsp,8
handler_no_err:
	push	rax
	; FIXME If we got here by interrupting kernel code, don't swapgs
	swapgs
	inc	qword [gs:32]
	mov	eax, dword [0xe390]
	mov	dword [gs:36], eax
	mov	dword [0xe380],10000 ; APICTI
	mov	dword [0xe0b0],0 ; EndOfInterrupt
	swapgs
	pop	rax
	iretq

syscall_entry_compat:
	ud2

syscall_entry:
	; r11 = old rflags
	; rcx = old rip
	; rax = syscall number, rax = return value (error code)

	; interrupts are disabled the whole time, TODO enable interrupts after switching GS and stack

	; TODO Update to match linux syscall clobbering convention:
	; - which regs have to be callee-saved?
	; - reset non-saved registers to 0 to avoid leaking information
	; - switch_to must know to restore callee-saved registers

	swapgs
	mov	[gs:0], rsp
	mov	esp, 0x10000
	push	rcx
	push	rdx
	push	rbx
	push	rdi
	test	rax,rax
	jz	.syscall_write
	cmp	eax,1
	je	.syscall_gettime
	cmp	eax,2
	je	.syscall_yield

.syscall_exit:
	pop	rdi
	pop	rbx
	pop	rdx
	pop	rcx
	mov	rsp, [gs:0]
	swapgs
	o64 sysret

	; Syscall #0: write byte to screen
.syscall_write:
	xchg	eax,ebx ; put the byte in eax instead of ebx, ebx now = 0

	mov	rdi, [gs:rbx+16] ; current pointer
	;mov	rbx, [gs:24] ; end of screen
	cmp	rdi, [gs:rbx+24] ; end of screen
	cmovge	rdi, [gs:rbx+8] ; beginning of screen, if current >= end

	cmp	al,10
	je .newline
.write_char:
	mov	ah, 0x0f
	stosw
.finish_write:
	xor	eax,eax
	mov	[gs:rax+16], rdi ; new pointer, after writing
	jmp	short .syscall_exit
.newline:
	mov	rax, rdi
	sub	rax, [gs:8] ; Result fits in 16 bits.
	cwd
	mov	bx, 160
	div	bx
	; dx now has the remainder
	mov	ecx,160
	sub	cx,dx
	shr	cx,1
	mov	ax,0x0f00+'-'
	rep	stosw

	jmp	.finish_write

.syscall_gettime:
	movzx	rax,byte [gs:rax+31] ; ax=1 when we get here
	jmp	.syscall_exit

.syscall_yield:
	; TODO
	; - Save enough state for a future FASTRET to simulate the right kind of
	; return:
	;   - Move saved callee-save registers from stack to PCB
	;   - Save rcx as .rip, r11 as .rflags
	; - Pop next process from run-queue
	; - Put old process at end of run-queue
	; - Load next-process pointer into rax
	jmp	switch_to

	times 4096-($-$$) db 0
__DATA__:

message:
	dq 0x0747074e074f074c, 0x07450744074f074d

counter:
	dq 32

tss:
	dd 0 ; Reserved
	dq 0x10000 ; Interrupt stack when interrupting non-kernel code and moving to CPL 0
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
	define_gate64 code64_seg,kernel_base+0x8000+handler_err-$$,GATE_PRESENT|GATE_TYPE_INTERRUPT
%define default_no_error \
	define_gate64 code64_seg,kernel_base+0x8000+handler_no_err-$$,GATE_PRESENT|GATE_TYPE_INTERRUPT
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

