; vim:ts=8:sts=8:sw=8:filetype=nasm:
; This is the real bootstrap of the kernel, and it is this part that is loaded
; by the boot sector (boot.asm)

[map all kstart.map]

CR0_PE		equ	0x00000001
CR0_MP		equ	0x00000002
CR0_EM		equ	0x00000004
CR0_TS_BIT	equ	3
CR0_PG		equ	0x80000000

CR4_PAE		equ	0x020
CR4_MCE		equ	0x040
CR4_PGE		equ	0x080
CR4_PCE		equ	0x100
CR4_OSFXSR	equ	0x200
CR4_OSXMMEXCPT	equ	0x400

%macro zero 1
	xor %1,%1
%endmacro

%include "msr.inc"
%include "proc.inc"
%include "segments.inc"

RFLAGS_IF_BIT	equ	9
RFLAGS_IF	equ	(1 << RFLAGS_IF_BIT)
RFLAGS_VM	equ	(1 << 17)

APIC_TICKS	equ	10000
APIC_PBASE	equ	0xfee00000

SYSCALL_WRITE	equ	0
SYSCALL_GETTIME	equ	1
SYSCALL_YIELD	equ	2

kernel_base equ -(1 << 30)

; Per-CPU data (theoretically)
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

	; Process last in use of the floating point registers
	.fpu_process	resq 1

	.free_frame	resq 1
	.temp_xmm0	resq 2
endstruc

%macro respage 0-1 1
	resb (4096*%1)
%endmacro
struc pages, 0x8000
.kernel		respage
.memory_map	respage

.page_tables:
.pml4		respage
.low_pdp	respage
.low_pd		respage
.low_pt		respage
.page_tables_end:

.apic		respage
.kernel_stack	respage
.kernel_stack_end:
.user_stack	respage
.user_stack_end:

.kernel_pdp	respage
.gseg_cpu0	respage
endstruc

kernel_stack_end equ pages.kernel_stack_end
user_stack_end equ pages.user_stack_end
APIC_LBASE equ pages.apic

section .text vstart=0x8000
section .data vfollows=.text follows=.text align=4
section usermode vfollows=.data follows=.data align=1
section bss nobits align=8
section memory_map nobits vstart=0x9000
section core nobits vstart=((1 << 64) - (1 << 30))

; get the physical address of a symbol in the .text section
%define text_paddr(sym) (section..text.vstart + sym - start16)
; get the virtual (kernel) address for a symbol in the .text section
%define text_vpaddr(sym) phys_vaddr(text_paddr(sym))
; translate a physical address to a virtual address in the 'core'
; Note: in most cases, RIP-relative addressing is enough (since we tell the
; assembler we're based at 0x8000 while we're actually at kernel_base+0x8000),
; but this can be used for constant data or wherever we need the full address.
%define phys_vaddr(phys) kernel_base + phys

; Note: Must all be in the same section, otherwise trouble with complicated
; expressions that rely on bitwise arithmetic on symbol relocations
section .text
bits 16
%include "start16.inc"
; start16 jumps to start32
bits 32
%include "start32.inc"
; start32 jumps to start64
bits 64
default rel
start64:
	; Start by jumping into the kernel memory area at -1GB. Since that's a
	; 64-bit address, we must do it in long mode...
	jmp	phys_vaddr(.moved)
.moved:
	mov	al,data64_seg
	mov	ah,0
	mov	ds,ax
	mov	ss,ax
	; TODO Should we reset fs and gs too?

	lea	rdi,[rel section.bss.vstart]
	lea	rcx,[rel section.bss.end]
	sub	rcx,rdi
	shr	rcx,3
	zero	eax
	rep	stosq

	mov	rdi,phys_vaddr(0xb8004)
	lea	rsi,[rel message]
	movsq
	movsq

	mov	ax,tss64_seg
	ltr	ax
	mov	rsp, phys_vaddr(kernel_stack_end)

	mov	ecx, MSR_APIC_BASE
	rdmsr
	; Clear high part of base address
	xor	edx,edx
	; Set base address, enable APIC (0x800), and set boot-strap CPU
	mov	eax, APIC_PBASE | 0x800 | 0x100
	wrmsr

APIC_REG_TPR		equ	0x80
APIC_REG_EOI		equ	0xb0
APIC_REG_SPURIOUS	equ	0xf0
; Bit in APIC_REG_SPURIOUS
APIC_SOFTWARE_ENABLE	equ	0x100

APIC_REG_TIMER_LVT	equ	0x320
APIC_REG_PERFC_LVT	equ	0x340
APIC_REG_LINT0_LVT	equ	0x350
APIC_REG_LINT1_LVT	equ	0x360
APIC_REG_ERROR_LVT	equ	0x370
APIC_REG_APICTIC	equ	0x380
APIC_REG_APICTCC	equ	0x390
APIC_REG_TIMER_DIV	equ	0x3e0

%assign rbpoffset 0x380

	mov	ebp,APIC_LBASE+rbpoffset
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
	zero	eax
	mov	cr8,rax


	mov	ecx, MSR_STAR
	; cs for syscall (high word) and sysret (low word).
	; cs is loaded from selector or selector+16 depending on whether we're returning to compat (+16) or long mode (+0)
	; ss is loaded from cs+8 (where cs is the cs selector chosen above)
	mov	edx,((user_code_seg | 11b) << 16) | code64_seg
	wrmsr

	inc	ecx ; c000_0082h - LSTAR
	lea	rax,[rel syscall_entry]
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

	; This is the kernel GS (it is in fact global, but it should be the per-cpu thingy)
	mov	eax, pages.gseg_cpu0
	cdq
	mov	ecx, MSR_GSBASE
	wrmsr

	; after this, ebx should be address to video memory and edi points to
	; the gs-segment data block
	cdqe
	mov	rdi,rax
	stosq ; gs:0 - selfpointer
	mov	rax,phys_vaddr(0xb8000)
	;mov	eax,0xb8000
	stosq ; gs:8 - VGA buffer base
	lea	rax,[rax+32] ; 32 means start 16 characters into the first line
	stosq ; gs:16 - VGA writing position
	lea	rax,[rax+80*25*2-32]
	stosq ; gs:24 - VGA buffer end
	zero	eax
	stosq ; gs:32/36 - current time (and tick)
	stosq ; gs:40 - user-mode stack seg
	stosq ; gs:48 - current process
	stosq ; gs:48 - runqueue
	stosq ; gs:56 - runqueue_last
	stosq ; gs:64 - fpu_process, the last process to have used the FPU
	stosq ; gs:72 - free_frame (starts as zero, cpu:s will bootstrap by asking from global pool)
	; gs:80,88 - temporary storage for page clearing function.
	stosq
	stosq

E820_MEM	equ 1
E820_RESERVED	equ 2
E820_ACPI_RCL	equ 3
; There is also 4, which is some ACPI thingy that we shouldn't touch

init_frames:
	; pointer to last page linked, will be stored later on in garbage-frame list
	zero	ecx

	; r8: kernel_base
	; r9d: first page to actually use, reserving:
	; 0..0x8000 for null pointers and waste,
	; 0x8000..0x13000 for kernel crap,
	; 0x13000..0x100000 because it contains fiddly legacy stuff
	mov	r8, phys_vaddr(0)
	mov	r9d, 0x100000
	zero	r10

	mov	rbp, phys_vaddr(0xb8020)
	lea	rsi, [memory_map.size]
	lodsd
	mov	ebx, eax
	add	rbx, rsi ; rbx is now end-of-buffer
.loop:
	cmp	rbx, rsi
	jb	.done
	lodsq
	mov	rdi, rax ; start of range
	lodsq
	mov	rdx, rax ; length of range
	lodsd
	cmp	eax, E820_MEM
	; TODO ranges with value 3 (E820_ACPI_RECLAIM) are reusable once we're done with the ACPI data in them
	jne	.loop
	add	ax,0f30h
	mov	word [rbp], ax
	cmp	rdi, r9
	cmovb	rdi, r9

	; rdi..rdx is a range of pages we should link into the garbage-page list

	; Add the offset to the mapping of physical memory.
	add	rdx, r8
	add	rdi, r8

.inner:
	cmp	rdi, rdx
	jge	.loop

	mov	[rdi], rcx
	mov	rcx, rdi
	inc	r10

	add	rdi, 4096
	jmp	.inner
.done:
	mov	[globals.garbage_frame], rcx

test_alloc:
	zero	ebx
	zero	ebp
.loop:
	call	allocate_frame
	test	rax,rax
	jz	.done
	inc	rbx
	mov	[rax],rbp
	mov	rbp,rax
	;mov	rdi,rbx
	;call	print_hex
	jmp	.loop

.done:
	mov	rdi,rbp
	test	rdi,rdi
	jz	launch_user
	mov	rbp,[rdi]
	call	free_frame
	jmp	.done

launch_user:
	call	allocate_frame
	o64 fxsave [rax]
	mov	[rel globals.initial_fpstate], rax

	; Make the first use of fpu/multimedia instructions cause an exception
	mov	rax,cr0
	bts	rax,CR0_TS_BIT
	mov	cr0,rax

	mov	edi, user_entry_2
	mov	esi, user_stack_end
	call	new_proc

	zero	ebx
	mov	rbx, [gs:rbx + gseg.self]
	mov	[rbx + gseg.runqueue], rax
	mov	rbp, rax

	mov	edi, user_entry_3
	mov	esi, user_stack_end
	call	new_proc
	mov	[rbx + gseg.runqueue_last], rax
	mov	[rbp + proc.next], rax

	mov	edi, user_entry
	mov	esi, user_stack_end
	call	new_proc

	jmp	switch_to

; note to self:
; callee-save: rbp, rbx, r12-r15
; caller-save: rax, rcx, rdx, rsi, rdi, r8-r11
; rsp must be restored when returning (obviously...)

; return value: rax, rdx (or by adding out-parameter)

; arguments: rdi, rsi, rdx, rcx (r10 in syscall), r8, r9

; arg0/rdi = entry-point
; arg1/rsi = stack
; returns process-struct pointer in rax
; all "saved" registers are set to 0, except rip and rflags - use other
; functions to e.g. set the address space and link the proc. into the runqueue
new_proc:
	push	rdi
	push	rsi
	call	allocate_frame

	; rax = proc page
	; rsi = entry-point
	; rcx = stack pointer
	pop	rcx
	pop	rsi

	test	eax,eax
	jz	.oom_no_pop

	push	rbx
	lea	rbx, [rax-proc]
	mov	[rbx+proc.rip],rsi
	mov	[rbx+proc.rsp],rcx
	; bit 1 of byte 1 of rflags: the IF bit
	; all other bits are set to 0 by default
	mov	byte [rbx+proc.rflags+1], RFLAGS_IF >> 8
	; Copy initial FPU/Media state to process struct
	mov	rsi, [rel globals.initial_fpstate]
	lea	rdi, [rbx+proc.fxsave]
	mov	ecx, 512 / 8
	rep	movsq

	; Allocate page table
	call	allocate_frame
	test	eax,eax
	jz	.oom

	mov	rdi, rax
	mov	rsi, pages.pml4
	mov	ecx, 4096 / 8
	rep	movsq
	sub	rax, phys_vaddr(0)
	mov	[rbx + proc.cr3], rax

	mov	rax,rbx
.oom:
	pop	rbx
.oom_no_pop:
	ret

switch_next:
	mov	bx, 0x1f00 | 'N'
	call	kputchar
	call	print_procstate

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

	; rcx = last, rax = next, rdx = old/current/new last
	; note: we've already checked that the runqueue is not empty, so we
	; must have a last-pointer too.
	zero	esi

	; LAST->next = (LAST == NEXT ? NULL : OLD)
	mov	rbx,rdx
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
	zero	ecx
	; OLD->next = NEXT->next = NULL (OLD because it's the tail of the list, NEXT because it's now unlinked)
	mov	[rax+proc.next],rcx ; Clear next-pointer since we're not linked anymore.
	mov	[rdx+proc.next],rcx ; Clear next-pointer since we're not linked anymore.

	mov	bx, 0x4f00 | 'N'
	call	kputchar
	mov	rbx, rax
	call	print_proc
	call	print_procstate

	jmp	switch_to

kputchar:
	push	rsi
	mov	rsi, [rdi+gseg.vga_pos]
	mov	[rsi], bx
	add	rsi, 2
	cmp	rsi, [rdi+gseg.vga_end]
	cmove	rsi, [rdi+gseg.vga_base]
	mov	[rdi+gseg.vga_pos], rsi
	pop	rsi
	ret

print_proc:
	push	rbx
	shr	ebx, 12
	and	ebx, 0xf
	add	bx, 0x1f00 | '@'
	call	kputchar
	pop	rbx
	ret

; requires rdi = gseg self-pointer
print_procstate:
	push	rbx
	push	rax
	mov	bx, 0x1f00 | '<'
	call	kputchar
	mov	rbx, [rdi+gseg.process]
	call	print_proc
	mov	bx, 0x1f00 | ':'
	call	kputchar
	mov	rbx, [rdi+gseg.runqueue]
.loop:
	call	print_proc
	test	rbx,rbx
	jz	.end_of_q
	mov	rax, [rbx+proc.next]
	cmp	rax, rbx
	mov	rbx, rax
	je	.error
	jmp	.loop

.error:
	call	print_proc
	mov	bx, 0x4f00 | '!'
	call	kputchar
	cli
	hlt

.end_of_q:
	mov	bx, 0x1f00 | ':'
	call	kputchar
	mov	rbx, [rdi+gseg.runqueue_last]
	call	print_proc
	mov	bx, 0x1f00 | '>'
	call	kputchar
	pop	rax
	pop	rbx
	;cli
	;hlt
	ret

; Takes process-pointer in rax, never "returns" to the caller (just jmp to it)
; All registers other than rax will be ignored, trampled, and replaced with the
; new stuff from the switched-to process
switch_to:
	cli	; I don't dare running this thing with interrupts enabled.
	clts	; We'll set it again below, if appropriate

	; Update pointer to current process
	zero	edi
	mov	rdi, [gs:rdi + gseg.self]

	mov	bx, 0x1f00 | 'T'
	call	kputchar
	mov	rbx, rax
	call	print_proc
	call	print_procstate

	mov	[rdi+gseg.process], rax

	mov	rbx, cr0
	; If switching back before anything else uses the FPU, don't set TS
	cmp	rax, [rdi+gseg.fpu_process]
	je	.no_set_ts
	bts	rbx, CR0_TS_BIT
	mov	cr0, rbx
.no_set_ts:

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
	; regs, rflags, push new rip and do a near return
	push	data64_seg
	push	qword [rax+proc.rsp]
	push	qword [rax+proc.rflags]
	push	code64_seg
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
	push	user_ds
	push	qword [rax+proc.rsp]
	push	qword [rax+proc.rflags]
	push	user_cs
.restore_and_iretq:
	push	qword [rax+proc.rip]
	; rax is first, but we'll take it last...
	lea	rsi, [rax+proc.rax+8]

%macro lodregs 1-*
	%rep %0
	%ifidni %1,skip
	lea	rsi,[rsi+8]
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
.fast_ret:
%macro load_regs 1-*
	%rep %0
	%ifidni %1,rax
	%error rax is in use by this macro
	%else
	mov	%1,[rax+proc. %+ %1]
	%endif
	%rotate 1
	%endrep
%endmacro

	load_regs rbp,rbx,r12,r13,r14,r15
.fast_fastret:
	mov	rsp, [rax+proc.rsp]
	mov	rcx, [rax+proc.rip]
	mov	r11, [rax+proc.rflags]
	mov	rax, [rax+proc.rax]
	o64 sysret

.ret_no_intrs:
	cli
	hlt

; End switch

; Allocate a frame and return its *virtual* address in the physical-memory
; mapping area. Subtract kernel_base (eugh, wrong name) to get the actual
; physical address.
allocate_frame:
	zero	edi
	mov	rdi, [gs:rdi]
; Use when the gseg is already stored in rdi
.have_gseg:
	mov	rax, [rdi+gseg.free_frame]
	test	rax, rax
	; If local stack is out of frames, steal from global stack
	jz	.steal_global_frames

	mov	rsi, [rax]
	mov	[rdi+gseg.free_frame], rsi
	zero	esi
	mov	[rax], rsi
	ret

.steal_global_frames:
	; TODO acquire global-page-structures spinlock
	mov	rax, [globals.free_frame]
	test	rax,rax
	jz	.clear_garbage_frame

	mov	rcx, [rax]
	test	ecx, ecx
	jz	.skip_steal2
	; If we can, steal two pages :)
	mov	rsi, [rcx]
	mov	[rdi+gseg.free_frame], rcx
	mov	rcx, rsi
.skip_steal2:
	mov	[globals.free_frame], rcx
	; TODO release global-page-structures spinlock
	ret

.clear_garbage_frame:
	mov	rax, [globals.garbage_frame]
	test	rax,rax
	jz	.ret_oom

	mov	rcx, [rax]
	mov	[globals.garbage_frame], rcx

	mov	rsi, rax
	add	rsi, 128
	mov	ecx, 16
	; Clear the task-switched flag while we reuse some registers
	mov	rdx, cr0
	clts
	movdqu	[rdi+gseg.temp_xmm0], xmm0
	xorps	xmm0, xmm0
.loop:
%assign i -128
%rep 16
	movntdq	[rsi + i], xmm0
%assign i i+16
%endrep
	add	rsi, 16*16
	loop	.loop
	movdqu	xmm0, [rdi+gseg.temp_xmm0]
	; Reset TS to whatever it was before
	mov	cr0, rdx

.ret_oom:
	; TODO release global-page-structures spinlock
	ret

; CPU-local garbage-frame stack? Background process for trickling cleared pages into cpu-local storage?
free_frame:
	; TODO acquire global-page-structures spinlock
	lea	rax, [rel globals.garbage_frame]
	mov	rcx, [rax]
	mov	[rdi], rcx
	mov	[rax], rdi
	; TODO release global-page-structures spinlock
	ret

handler_err:
handler_no_err:
	cli
	hlt

%assign i 0
%rep 18
handler_n %+ i:
	mov	eax, i
	cli
	hlt
%assign i i+1
%endrep

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
	mov	eax, dword [abs APIC_LBASE + APIC_REG_APICTCC]
	mov	dword [rdi+gseg.tick], eax
	mov	dword [abs APIC_LBASE + APIC_REG_APICTIC], APIC_TICKS
	mov	dword [abs APIC_LBASE + APIC_REG_EOI], 0

	mov	rax,[rdi+gseg.process]
	; The rax and rdi we saved above, store them in process
	pop	qword [rax+proc.rdi]
	pop	qword [rax+proc.rax]
	call	save_from_iret
	mov	rdx, rax
	mov	rax, [rdi+gseg.runqueue]
	jmp	switch_next

handler_NM: ; Device-not-present, fpu/media being used after a task switch
	push	rdi
	push	rax
	; FIXME If we get here in kernel mode?
	swapgs

	clts

	; Find the previous process and save fpu/media state to its process struct
	zero	edi
	mov	rdi,[gs:edi]
	mov	rax,[rdi+gseg.fpu_process]
	test	rax,rax
	; No previous process has unsaved fpu state, just load this process' state
	jz	.no_save_state

	; Save FPU state in process rax
	o64 fxsave [rax+proc.fxsave]
.no_save_state:
	mov	rax,[rdi+gseg.process]
	o64 fxrstor [rax+proc.fxsave]

.ret:
	; FPU state now owned by current process
	mov	[rdi+gseg.fpu_process], rax

	pop	rax
	pop	rdi
	swapgs
	iretq

syscall_entry_compat:
	ud2

; note to self:
; callee-save: rbp, rbx, r12-r15
; caller-save: rax, rcx, rdx, rsi, rdi, r8-r11
; rsp must be restored when returning (obviously...)

; return value: rax, rdx (or by adding out-parameter)

; arguments: rdi, rsi, rdx, rcx (r10 in syscall), r8, r9

syscall_entry:
	; r11 = old rflags
	; rcx = old rip
	; rax = syscall number, rax = return value (error code)

	; interrupts are disabled the whole time, TODO enable interrupts after switching GS and stack

	mov	word [rel 0xb8002], 0xf00|'S'

	swapgs
	mov	[gs:gseg.user_rsp], rsp
	; Hardcoded kernel stack
	mov	rsp, phys_vaddr(kernel_stack_end)
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
	pop	rcx
.sysret_rcx_set:
	; Be paranoid and evil - explicitly clear everything that we could have
	; ever clobbered.
	; rax, rcx, r11 are also in this list, but are used for return, rip and rflags respectively.
	zero	edx ; If we start returning more than one 64-bit value
	zero	esi
	zero	edi
	zero	r8
	zero	r9
	zero	r10

	mov	rsp, [gs:gseg.user_rsp]
	swapgs
	o64 sysret

.syscall_gettime:
	movzx	rax,byte [gs:rax+gseg.curtime-1] ; ax=1 when we get here
	jmp	.sysret

.syscall_yield:
	zero	edi
	mov	rdi, [gs:rdi+gseg.self]

	; - Load current process pointer into rax
	mov	rax,[rdi+gseg.process]

	; - Save enough state for a future FASTRET to simulate the right kind of
	; return:
	;   - Move saved callee-save registers from stack to PCB
%macro save_regs 1-*
%rep %0
	mov qword [rax+proc.%1], %1
	%rotate 1
%endrep
%endmacro
	save_regs rbp,rbx,r12,r13,r14,r15

	; callee-save stuff is now saved and we can happily clobber!
	; Except we can't! :) The .no_yield path (empty runqueue, errors) will
	; sysret directly and requires that we didn't break anything.

	; Set the fast return flag to return quickly after the yield
	bts	qword [rax+proc.flags], PROC_FASTRET

	; Return value: always 0 for yield
	zero	edx
	mov	[rax+proc.rax], rdx

	; The rcx we pushed in the prologue
	pop	qword [rax+proc.rip]
	mov	[rax+proc.rflags], r11

	; Get the stack pointer, save it for when we return.
	; TODO We should store it directly in the right place in the prologue.
	mov	rdx, [rdi+gseg.user_rsp]
	mov	[rax+proc.rsp], rdx

	; - Load next-process pointer, bail out back to normal return if equal
	mov	rdx, [rdi+gseg.runqueue]
	; Ran out of runnable processes?
	test	rdx,rdx
	jz	.no_yield

	xchg	rax,rdx

	; switch_next takes switch-to in rax, switch-from in rdx
	jmp	switch_next

.no_yield:
	mov	rcx, [rax+proc.rip]
	zero	eax
	jmp	.sysret_rcx_set


section usermode

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
	mov	ebx, 2
	movq	xmm1, rbx
	movq	xmm0, xmm1
.loop:
	mov	edi,'2'
	xor	eax,eax
	syscall

	paddq	xmm0,xmm1

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	.loop

user_entry_3:
	mov	ebx, 7
	movq	xmm1, rbx
	movq	xmm0, xmm1
.loop:
	lea	edi,['a'+ebx]
	xor	eax,eax
	syscall

	paddq	xmm0, xmm1
	mov	eax, SYSCALL_YIELD
	syscall
	dec	ebx
	jnz	.loop

.end:
	lea	rdi,[rel .test_message]
	call	puts

	lea	rdi,[rel .test_format]
	lea	rsi,[rel .test_arg1]
	mov	edx,'C'
	call	printf

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	.end

.test_message:
	db	'Hello World from puts',10,0
.test_format:
	db	'printf %% "%s" %c',10,0
.test_arg1:
	db	'Hello World',0

puts:
	; callee-save: rbp, rbx, r12-r15
	push	rbp
	push	rbx
	mov	rbp,rdi

.loop:
	mov	dil,[rbp]
	test	dil,dil
	jz	.ret
	inc	rbp

	mov	eax,SYSCALL_WRITE
	syscall
	jmp	.loop

.ret:
	pop	rbx
	pop	rbp
	ret

printf:
	; al: number of vector arguments (won't be used...)
	; rdi: format string
	; rsi,rdx,rcx,r8,r9: consequtive arguments

	; reorder the stack a bit so that we have all our parameters in a row
	mov	[rsp-32],rsi
	mov	rsi,[rsp]
	mov	[rsp-40],rsi ; rsp-40 is now the return address!
	mov	[rsp-24],rdx
	mov	[rsp-16],rcx
	mov	[rsp-8],r8
	mov	[rsp],r9
	sub	rsp,40
	; rdi: pointer to parameters
	; rsi: pointer to format string
	mov	rsi,rdi
	lea	rdi,[rsp+8]

.nextchar:
	lodsb
	test	al,al
	jz	.done
	cmp	al,'%'
	je	.handle_format

.write_al:
	mov	r12,rdi
	mov	r13,rsi
	mov	edi,eax
	mov	eax,SYSCALL_WRITE
	syscall
	mov	rsi,r13
	mov	rdi,r12
	jmp	.nextchar

.handle_format:
	lodsb
	cmp	al,'%'
	je	.write_al
	cmp	al,'c'
	je	.fmt_c
	cmp	al,'s'
	je	.fmt_s

.fmt_c:
	mov	rax,[rdi]
	add	rdi,8
	jmp	.write_al

.fmt_s:
	mov	rbp,[rdi]
	; syscall will clobber rsi and rdi but not r12 and r13
	lea	r13,[rdi+8]
	mov	r12,rsi

	mov	rdi,rbp
	call	puts

	mov	rsi,r12
	mov	rdi,r13
	jmp	.nextchar

.done:
	add	rsp,48
	jmp	[rsp-48]

section .data

message:
	dq 0x0747074e074f074c, 0x07450744074f074d

section bss
globals:
; Pointer to first free page frame..
.free_frame	resq 1
; free frames that are tainted and need to be zeroed before use
.garbage_frame	resq 1
; Pointer to initial FPU state, used to fxrstor before a process' first use of
; media/fpu instructions. Points to a whole page but only 512 bytes is actually
; required.
.initial_fpstate	resq 1

section .text
tss:
	dd 0 ; Reserved
	dq phys_vaddr(kernel_stack_end) ; Interrupt stack when interrupting non-kernel code and moving to CPL 0
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
	define_tss64 0x68, text_paddr(tss)
	; USER segments
	; 32-bit code/data. Used for running ancient code in compatibility mode. (i.e. nothing)
	define_segment 0xfffff,0,RX_ACCESS | GRANULARITY | SEG_32BIT | SEG_DPL3
	define_segment 0xfffff,0,RW_ACCESS | GRANULARITY | SEG_32BIT | SEG_DPL3
	; 64-bit code/data. Used.
	define_segment 0,0,RX_ACCESS | SEG_64BIT | SEG_DPL3
	define_segment 0,0,RW_ACCESS | SEG_DPL3
gdt_end:

section .data
align	4
gdtr:
	dw	gdt_end-gdt_start-1 ; Limit
	dd	gdt_start  ; Offset

section .text
idt:
%macro interrupt_gate 1
	define_gate64 code64_seg,text_vpaddr(%1),GATE_PRESENT|GATE_TYPE_INTERRUPT
%assign i i+1
%endmacro
%define default_error interrupt_gate (handler_n %+ i)
%define default_no_error interrupt_gate (handler_n %+ i)
%define null_gate define_gate64 0,0,GATE_TYPE_INTERRUPT

	; exceptions with errors:
	; - 8/#DF/double fault (always zero)
	; - 10/#TS/Invalid-TSS
	; - 11/#NP/Segment-Not-Present
	; - 12/#SS/Stack Exception
	; - 13/#GP/General Protection
	; - 14/#PF/Page Fault
	; - 17/#AC/Alignment Check

%assign i 0
	; 0-6
	%rep 7
	default_no_error
	%endrep
	interrupt_gate handler_NM ; vector 7, #NM, Device-Not-Available
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

section .data
idtr:
	dw	idt_end-idt-1
	dd	idt

section memory_map
memory_map:
.size	resd	1
.data:

section bss
align 8
section.bss.end:

section memory_map
section.memory_map.end:

section .data
section.data.end:

section usermode
section.usermode.end:

