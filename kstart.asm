; vim:ts=8:sts=8:sw=8:filetype=nasm:
; This is the real bootstrap of the kernel, and
; it is this part that is loaded by the boot sector (boot.asm)

org 0x8000
bits 16
start16:
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

	; Safe area to put random crap like the e820 address map.
	mov	ax,0x1400
	mov	es,ax
	mov	di,4
	xor	ebx,ebx
.e820_loop:
	mov	eax,0xe820 ; Function code
	; mov bx, -- ; ebx contains the "Continuation value", returned by last call (or 0 on first iteration)
	mov	cx,20 ; Size of output element
	mov	edx,'PAMS'

	int	15h
	add	di,20

	jc	.done
	cmp	eax,'PAMS'
	jne	.done
	test	ebx,ebx
	jz	.done

	jmp	.e820_loop
.done:
	mov	dword [es:0], edi

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

%include "msr.inc"

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

struc	proc, -0x80
	.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15

	; Aliases for offsets into regs
	.rax	equ	.regs+(0*8)
	.rcx	equ	.regs+(1*8)
	.rbx	equ	.regs+(3*8)
	.rsp	equ	.regs+(4*8)
	.rbp	equ	.regs+(5*8)
	.rsi	equ	.regs+(6*8)
	.rdi	equ	.regs+(7*8)
	%assign i 8
	%rep 8
	.r%+i	equ	.regs+(i*8)
	%assign i i+1
	%endrep

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

RFLAGS_IF_BIT	equ	9
RFLAGS_IF	equ	(1 << RFLAGS_IF_BIT)
RFLAGS_VM	equ	(1 << 17)

APIC_TICKS	equ	10000
APIC_LBASE	equ	0x0000e000
APIC_PBASE	equ	0xfee00000

SYSCALL_WRITE	equ	0
SYSCALL_GETTIME	equ	1
SYSCALL_YIELD	equ	2

struc	gseg
	; Pointer to self
	.self	resq	1
	.vga_base	resq 1
	.vga_pos	resq 1
	.vga_end	resq 1
	
	.curtime	resd 1
	.tick		resd 1

	.user_rsp	resq 1
	.process	resq 1
	.runqueue	resq 1
	.runqueue_last	resq 1
endstruc

%macro zero 1
	xor %1,%1
%endmacro

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
	; And more flags
	; - Page-level cache disable (bit 4) used for APIC
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

	zero	eax
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

	mov	ecx, MSR_EFER
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
	; Start by jumping into the kernel memory area at -1GB. Since that's a
	; 64-bit address, we must do it in long mode...
	lea	rax,[rel kernel_base+.moved]
	jmp	rax
.moved:
	mov	ax,data64_seg
	mov	ds,ax
	mov	ss,ax
	; TODO Should we reset fs and gs too?

	lea	rdi,[rel 0xb8004]
	lea	rsi,[rel message]
	movsq
	movsq

	mov	ax,tss64_seg
	ltr	ax
	lea	rsp, [rel 0x11000]

	; Set page 0xe000 to uncacheable and map it to the APIC address.
	mov	eax, APIC_PBASE | 0x13
	;movzx	rax, eax
	mov	qword [rel 0xd000+(APIC_LBASE >> 12)*8], rax
	invlpg	[abs APIC_LBASE]

	mov	ecx, MSR_APIC_BASE
	rdmsr
	; Clear high part of base address
	xor	edx,edx
	; Set base address, enable APIC (0x800), and set boot-strap CPU
	mov	eax, APIC_PBASE | 0x800 | 0x100
	wrmsr

	mov	ebp,APIC_LBASE
	; TODO Should point into the kernel area, but the page must have the
	; right cache attributes (which the kernel area doesn't).
	;lea	rbp,[rel APIC_LBASE]

APIC_REG_APICTIC	equ	0x380
APIC_REG_TIMER_LVT	equ	0x320
APIC_REG_PERFC_LVT	equ	0x340
APIC_REG_LINT0_LVT	equ	0x350
APIC_REG_LINT1_LVT	equ	0x360
APIC_REG_ERROR_LVT	equ	0x370
APIC_REG_SPURIOUS equ 0xf0
APIC_SOFTWARE_ENABLE equ 0x100
APIC_REG_EOI	equ	0xb0
APIC_REG_TIMER_DIV	equ	0x3e0

%assign rbpoffset 0x380

	add	bp,rbpoffset
	mov	dword [rbp+APIC_REG_APICTIC-rbpoffset],APIC_TICKS
	mov	ax,1010b
	mov	dword [rbp+APIC_REG_TIMER_DIV-rbpoffset],eax  ; Divide by 128

	mov	dword [rbp+APIC_REG_TIMER_LVT-rbpoffset], 0x20020
	mov	dword [rbp+APIC_REG_PERFC_LVT-rbpoffset], 0x10000
	mov	dword [rbp+APIC_REG_LINT0_LVT-rbpoffset], 0x8700
	mov	dword [rbp+APIC_REG_LINT1_LVT-rbpoffset], 0x0400
	mov	dword [rbp+APIC_REG_ERROR_LVT-rbpoffset], 0x10000

	; Enable the APIC and set the spurious interrupt vector to 0xff
	xor	eax,eax
	mov	ax,APIC_SOFTWARE_ENABLE | 0xff
	or	dword [rbp+APIC_REG_SPURIOUS-rbpoffset],eax

	; Set end-of-interrupt flag so we get some interrupts.
	mov	dword [rbp+APIC_REG_EOI-rbpoffset],eax
	; Set the task priority register to 0 (accept all interrupts)
	mov	dword [abs 0xe080], 0



	mov	ecx, 0c000_0081h ; TODO Make symbolic
	; cs for syscall (high word) and sysret (low word).
	; cs is loaded from selector or selector+16 depending on whether we're returning to compat (+16) or long mode (+0)
	; ss is loaded from cs+8 (where cs is the cs selector chosen above)
	mov	edx,((user_code_seg | 11b) << 16) | code64_seg
	wrmsr

	inc	ecx ; c000_0082h - LSTAR
	lea	rax,[rel syscall.entry]
	mov	rdx,rax
	mov	eax,eax
	shr	rdx,32
	wrmsr

	inc	ecx ; c000_0083h - CSTAR
	mov	eax,syscall_entry_compat
	cdq
	wrmsr

	inc	ecx ; c000_0084h - SF_MASK
	mov	eax, RFLAGS_IF | RFLAGS_VM
	cdq
	wrmsr

	mov	ecx, MSR_EFER
	rdmsr
	bts	eax, 0 ; Set SCE
	wrmsr

	; This is the kernel GS, at 0x12000 (the top of the kernel stack)
	lea	eax, [rel 0x12000]
	cdq
	mov	ecx, MSR_GSBASE
	wrmsr

	; after this, ebx should be address to video memory and edi points to
	; the gs-segment data block
	cdqe
	mov	rdi,rax
	stosq ; gs:0 - selfpointer
	lea	rax,[rel 0xb8000]
	;mov	eax,0xb8000
	stosq ; gs:8 - VGA buffer base
	lea	rax,[rax+32] ; 32 means start 16 characters into the first line
	stosq ; gs:16 - VGA writing position
	lea	rax,[rax+80*25*2-32]
	stosq ; gs:24 - VGA buffer end
	zero	eax
	stosq ; gs:32/36 - current time (and tick)
	mov	eax,dword [rel user_proc_1-proc+proc.rsp]
	stosq ; gs:40 - user-mode stack seg
	zero	eax
	stosq ; gs:48 - current process
	lea	rax,[rel user_proc_2-proc]
	stosq ; gs:48 - runqueue
	stosq ; gs:56 - runqueue_last

	lea	rax,[rel user_proc_1-proc]
	jmp	switch_to

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
	ud2 ; FIXME This function is not updated for the offset proc struct.

	mov	rdx,rcx
	; rdi = proc
	; rdx = entry-point
	mov	cl,proc_size / 8
	movzx	rcx,cl
	xor	eax,eax
	rep stosq
	mov	[rdi-proc_size+proc.rip],rdx
	; bit 1 of byte 1 of rflags: the IF bit
	; all other bits are set to 0 by default
	mov	byte [rdi-proc_size+proc.rflags+1], RFLAGS_IF >> 8
	lea	rax, [rdi-proc_size]
	ret

; rax is already saved, and now points to the process base
; rdi is already saved (points to the gseg, but not used by this function)
; rsp, rip, rflags are on the stack in "iretq format"
; returns with ret; call with call
save_from_iret:
	; Save the rsp so we can fiddle with it here and restore before ret.
	mov	[rax+proc.rcx], rcx
	mov	rcx, rsp

	add	rsp,8 ; return address
	pop	qword [rax+proc.rip]
	add	rsp,8 ; CS
	pop	qword [rax+proc.rflags]
	pop	qword [rax+proc.rsp]
	add	rsp,8 ; SS

	; Reset fastret flag so that iretq is used next time
	btr	qword [rax+proc.flags], PROC_FASTRET

	; Now save GPR:s in process. rsp can be clobbered (we'll restore it),
	; and rcx is already saved.
	lea	rsp, [rax+proc.endregs] ; note: as long as endregs == 0, this has the same size as a plain mov, but this expression will keep working :)
	push	r15
	push	r14
	push	r13
	push	r12
	push	r11
	push	r10
	push	r9
	push	r8
	;.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15
	sub	rsp,8 ; rdi is already saved
	push	rsi
	push	rbp
	sub	rsp,8 ; rsp is already saved ... should rsp be stored outside the gpr file?
	push	rbx
	push	rdx
	; rcx is already saved
	; rax is already saved

	mov	rsp, rcx
	ret

switch_next:
	; Now NEXT (switching to) is in rax, and OLD (switching from) is in rdx
	; runqueue_last points to LAST
	; runqueue currently points to NEXT

	; After switch:
	; rax -> NEXT
	; [rdx -> OLD]
	; runqueue -> NEXT->next or OLD if NEXT->next = NULL
	; runqueue_last -> OLD
	; LAST->next -> OLD, or NULL if LAST == NEXT
	; OLD->next -> NULL
	; NEXT->next -> NULL

	; - Put old process at end of run-queue
	mov	rcx, [rdi+gseg.runqueue_last]
	mov	[rdi+gseg.runqueue_last], rdx ; runqueue_last = OLD

	; rcx = last, rax = next, rdx = current/new last
	; note: we've already checked that the runqueue is not empty, so we
	; must have a last-pointer too.
	xor	esi,esi

	; LAST->next = (LAST == NEXT ? NULL : OLD)
	mov	rbx,rcx
	cmp	rcx,rax ; if LAST == NEXT, we want to store NULL instead of OLD
	cmove	rbx,rsi
	mov	[rcx+proc.next], rbx

	; runqueue = (NEXT->next ? NEXT->next : OLD)
	; - Unqueue next-process
	mov	rcx, [rax+proc.next]
	test	rcx,rcx
	cmovz	rcx,rdx
	mov	[rdi+gseg.runqueue], rcx

	; - Load next-process pointer into rax
	xor	ecx,ecx
	; OLD->next = NEXT->next = NULL (OLD because it's the tail of the list, NEXT because it's now unlinked)
	mov	[rax+proc.next],rcx ; Clear next-pointer since we're not linked anymore.
	mov	[rdx+proc.next],rcx ; Clear next-pointer since we're not linked anymore.

	jmp	switch_to

; Takes process-pointer in rax, never "returns" to the caller (just jmp to it)
; All registers other than rax will be ignored, trampled, and replaced with the
; new stuff from the switched-to process
switch_to:
	cli	; I don't dare running this thing with interrupts enabled.

	; Update pointer to current process
	xor	edi,edi
	mov	[gs:rdi+gseg.process], rax

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

	bt	qword [rax+proc.rflags], RFLAGS_IF_BIT
	jnc	.ret_no_intrs

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

	; TODO Replace with stack operations: set rsp, then pop in the right
	; order. Then lay out process struct's rip, rflags, rsp so they can
	; be iretq'd directly afterwards. (Will cost 16 bytes for cs/ss...)
	; If not layout-hacked, we have to save our own rsp somewhere.

	;.regs	resq 16 ; a,c,d,b,sp,bp,si,di,r8-15
	; skip rsp (iret will fix that) and rsi (we're still using it for the lodsqs, for now)
	lodregs	rcx,rdx,rbx,SKIP,rbp,SKIP,rdi,r8,r9,r10,r11,r12,r13,r14,r15
	mov	rax, [rsi-proc.endregs+proc.rax]
	mov	rsi, [rsi-proc.endregs+proc.rsi]
	iretq

	; For a slightly slower "fast" return, also restore callee-save regs.
	; IPC:s will have a specific calling convention (which probably only defines r11 and rcx)
	;lodregs rbp,rbx,r12,r13,r14,r15
.fast_ret:
	mov	rsp, [rax+proc.rsp]
	mov	rcx, [rax+proc.rip]
	mov	r11, [rax+proc.rflags]
	mov	rax, [rax+proc.rax]
	o64 sysret

.ret_no_intrs:
	cli
	hlt

; End switch

handler_err:
handler_no_err:
	cli
	hlt

timer_handler:
	push	rax
	push	rdi
	; FIXME If we got here by interrupting kernel code, don't swapgs
	; But currently, all kernel code is cli, so we can only get here as the
	; result of a fault...
	swapgs

	mov	word [rel 0xb8002], 0x0700|'T'

	xor	edi, edi
	mov	rdi, [gs:rdi+gseg.self]
	inc	dword [rdi+gseg.curtime]
	mov	eax, dword [abs 0xe390]
	mov	dword [rdi+gseg.tick], eax
	mov	dword [abs 0xe380],APIC_TICKS ; APICTIC
	mov	dword [abs APIC_LBASE+APIC_REG_EOI],0 ; EndOfInterrupt

	mov	rax,[rdi+gseg.process]
	; The rax and rdi we saved above, store them in process
	pop	qword [rax+proc.rdi]
	pop	qword [rax+proc.rax]
	call	save_from_iret
	mov	rdx, rax
	mov	rax, [rdi+gseg.runqueue]
	jmp	switch_next

syscall_entry_compat:
	ud2

; note to self:
; callee-save: rbp, rbx, r12-r15
; caller-save: rax, rcx, rdx, rsi, rdi, r8-r11
; rsp must be restored when returning (obviously...)

; return value: rax, rdx (or by adding out-parameter)

; arguments: rdi, rsi, rdx, rcx (r10 in syscall), r8, r9

syscall:
.entry:
	; r11 = old rflags
	; rcx = old rip
	; rax = syscall number, rax = return value (error code)

	; interrupts are disabled the whole time, TODO enable interrupts after switching GS and stack

	; TODO Update to match linux syscall clobbering convention:
	; - reset non-saved registers to 0 to avoid leaking information
	; - save callee-saved registers in process struct when yielding
	; - switch_to must know to restore callee-saved registers

	mov	word [rel 0xb8002], 0xf00|'S'

	swapgs
	mov	[gs:gseg.user_rsp], rsp
	; Hardcoded kernel stack
	lea	rsp, [rel 0x10000]
	push	rcx
	cmp	eax, SYSCALL_WRITE
	je	.syscall_write
	cmp	eax, SYSCALL_GETTIME
	je	.syscall_gettime
	cmp	eax, SYSCALL_YIELD
	je	.syscall_yield
	jmp	.sysret

	; Syscall #0: write byte to screen
.syscall_write:
	mov	eax, edi ; put the byte in eax instead of edi, edi now = 0
	; FIXME Use a different register than dx so we can reuse the gseg-pointer...
	zero	edx
	mov	rdx, [gs:rdx+gseg.self]

	mov	rdi, [rdx+gseg.vga_pos] ; current pointer
	;mov	___, [gs:24] ; end of screen
	cmp	rdi, [rdx+gseg.vga_end] ; end of screen
	cmovge	rdi, [rdx+gseg.vga_base] ; beginning of screen, if current >= end

	cmp	al,10
	je .newline
.write_char:
	mov	ah, 0x0f
	stosw
.finish_write:
	xor	eax,eax
	mov	[gs:rax+gseg.vga_pos], rdi ; new pointer, after writing
	jmp	short .sysret
.newline:
	mov	rax, rdi
	sub	rax, [gs:gseg.vga_base] ; Result fits in 16 bits.
	cwd ; ax -> dx:ax
	mov	si, 160
	div	si
	; dx now has the remainder
	mov	ecx,160
	sub	cx,dx
	shr	cx,1
	mov	ax,0x0f00+'-'
	rep	stosw

	jmp	.finish_write

.sysret:
	; Be paranoid and evil - explicitly clear everything that we could have
	; ever clobbered.
	; rax, rcx, r11 are also in this list, but are used for return, rip and rflags respectively.
	zero	edx ; If we start returning more than one 64-bit value
	zero	esi
	zero	edi
	zero	r8
	zero	r9
	zero	r10

	pop	rcx
	mov	rsp, [gs:gseg.user_rsp]
	swapgs
	o64 sysret

.syscall_gettime:
	movzx	rax,byte [gs:rax+gseg.curtime-1] ; ax=1 when we get here
	jmp	.sysret

.syscall_yield:
	xor	edi,edi
	mov	rdi, [gs:rdi+gseg.self]

	; - Load current process pointer into rax
	mov	rax,[rdi+gseg.process]

	; - Save enough state for a future FASTRET to simulate the right kind of
	; return:
	;   - Move saved callee-save registers from stack to PCB
%macro save_regs 1-*
%rep %0
	mov qword [rax+proc.%+ %1], %1
	%rotate 1
%endrep
%endmacro
	save_regs rbp,rbx,r12,r13,r14,r15

	; callee-save stuff is now saved and we can happily clobber!

	; Set the fast return flag to return quickly after the yield
	bts	qword [rax+proc.flags], PROC_FASTRET

	; Return value: always 0 for yield
	xor	rbx,rbx
	mov	[rax+proc.rax], rbx

	; The rcx we pushed in the prologue
	pop	qword [rax+proc.rip]
	mov	[rax+proc.rflags], r11

	; Get the stack pointer, save it for when we return.
	; TODO We should store it directly in the right place in the prologue.
	mov	rbx, [rdi+gseg.user_rsp]
	mov	[rax+proc.rsp], rbx

	; - Load next-process pointer, bail out back to normal return if equal
	mov	rdx, [rdi+gseg.runqueue]
	; Same process on runqueue? (Probably really an error.)
	cmp	rdx,rax
	je	.no_yield
	; Ran out of runnable processes?
	test	rdx,rdx
	jz	.no_yield

	xchg	rax,rdx

	; switch_next takes switch-to in rax, switch-from rdx
	jmp	switch_next

.no_yield:
	xor	rax,rax
	jmp	.sysret


__USER__:

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

user_entry:
	xor	eax,eax

	mov	edi,'U'
	syscall
	mov	edi,10
	syscall
	mov	edi,'V'
	syscall
	mov	edi,10
	syscall

.loop:
	xor	eax,eax
	mov	edi,'|'
	syscall

	;xor	eax,eax
	;mov	edi,'Y'
	;syscall
	;mov	eax,SYSCALL_YIELD
	;syscall

	mov	eax,SYSCALL_GETTIME
	syscall
	movzx	edi,al
	xor	eax,eax ; SYSCALL_WRITE
	syscall

	mov	edi,10
	syscall

	mov	eax,SYSCALL_GETTIME
	syscall
	mov	ebp,eax
.notchanged:
	mov	eax,SYSCALL_GETTIME
	syscall
	cmp	al,bpl
	jne	.loop

	jmp	.notchanged

user_entry_2:
	mov	edi,'2'
	xor	eax,eax
	syscall

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	user_entry_2

	times 4096-($-$$) db 0
__DATA__:

message:
	dq 0x0747074e074f074c, 0x07450744074f074d

counter:
	dq 32

tss:
	dd 0 ; Reserved
	dq kernel_base+0x10000 ; Interrupt stack when interrupting non-kernel code and moving to CPL 0
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
%macro interrupt_gate 1
	define_gate64 code64_seg,kernel_base+0x8000+%1-$$,GATE_PRESENT|GATE_TYPE_INTERRUPT
%endmacro
%define default_error interrupt_gate handler_err
%define default_no_error interrupt_gate handler_no_err
%define null_gate define_gate64 0,0,GATE_TYPE_INTERRUPT

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
	interrupt_gate timer_handler ; APIC Timer
idt_end:

idtr:
	dw	idt_end-idt-1
	dd	idt

