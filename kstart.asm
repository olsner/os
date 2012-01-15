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

%macro restruc 1-2 1
	resb (%1 %+ _size) * %2
%endmacro
%macro respage 0-1 1
	resb (4096*%1)
%endmacro

; callee-save: rbp, rbx, r12-r15
; caller-save: rax, rcx, rdx, rsi, rdi, r8-r11
%macro clear_clobbered_syscall 0
	; rax, rcx, r11 are also in this list, but are used for return, rip and rflags respectively.
	zero	edx ; If we start returning more than one 64-bit value
	zero	esi
	zero	edi
	zero	r8
	zero	r9
	zero	r10
%endmacro
%macro clear_clobbered 0
	clear_clobbered_syscall
	zero	ecx
	zero	r11
%endmacro
; Most internal functions use a special convention where rdi points to the
; cpu-specific data segment. (Maybe we should change this to use e.g. rbp.)
%macro clear_clobbered_keeprdi 0
	zero	ecx
	zero	edx ; If we start returning more than one 64-bit value
	zero	esi
	zero	r8
	zero	r9
	zero	r10
	zero	r11
%endmacro

%macro pushsection 1
[section %1]
%endmacro
%macro popsection 0
__SECT__
%endmacro

%include "msr.inc"
%include "proc.inc"
%include "segments.inc"
%include "string.inc"

%define log_switch_to 1
%define log_switch_next 0

%assign need_print_procstate (log_switch_to | log_switch_next)

RFLAGS_IF_BIT	equ	9
RFLAGS_IF	equ	(1 << RFLAGS_IF_BIT)
RFLAGS_VM	equ	(1 << 17)

APIC_TICKS	equ	10000
APIC_PBASE	equ	0xfee00000

SYSCALL_WRITE	equ	0
SYSCALL_GETTIME	equ	1
SYSCALL_YIELD	equ	2
; Send a message without waiting for response, may need to wait for the
; recipient process to become ready (if it isn't in a RECV call). Or should it
; merely fail in that situation?
SYSCALL_ASEND	equ	3
; Send a message, wait synchronously for response
; rdi: message ID
; rsi: target process ID
SYSCALL_SENDRCV	equ	4
; Receive a message
; Returns in registers:
; rax: message ID
; rdx: sending process ID
; <syscall argument register>: message parameters
SYSCALL_RECV	equ	5
SYSCALL_SEND	equ	5
SYSCALL_NEWPROC	equ	6

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

%define msg_code(msg, error, respflag) \
	(msg | (error << 8) | (respflag << 9))
%define msg_resperr(msg) msg_code(msg, 1, 1)
%define msg_sendcode(msg) msg_code(msg, 0, 0)



kernel_base equ -(1 << 30)
handle_top equ 0x7fff_ffff_f000 ; For now, we waste one whole page per handle (I think)

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

struc pages, 0x8000
.kernel		respage 2
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

.kernel_pdp	respage
.gseg_cpu0	respage
endstruc

kernel_stack_end equ pages.kernel_stack_end

section .text vstart=0x8000
section .data vfollows=.text follows=.text align=4
section usermode vfollows=.data follows=.data align=1
section bss nobits align=8
section memory_map nobits vstart=pages.memory_map

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
	; Need to reload gdt and ldt, since they are using 32-bit addresses
	; (vaddr==paddr) that will get unmapped as soon as we leave the
	; boot code.
	lidt	[idtr]
	lgdt	[gdtr]

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

	; Clear out the page tables we're going to reuse
	zero	eax
	lea	rdi, [pages.low_pd]
	mov	ecx, 512
	rep	stosd

	; Clear old link that points to low_pdp (where these mappings will
	; otherwise be duplicated)
	mov	[pages.pml4], dword 0
	mov	[pages.kernel_pdp + 0xff0], dword pages.low_pd | 3
	mov	[pages.low_pd + 0xff8], dword pages.low_pt | 3
	; Set page kernel_base-0x1000 to uncacheable and map it to the APIC
	; address.
	mov	[pages.low_pt + 0xff8], dword APIC_PBASE | 0x13
	invlpg	[rel -0x1000]

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

	mov	rbp,phys_vaddr(pages.apic)+rbpoffset
	mov	ax,1010b
	mov	dword [rbp+APIC_REG_TIMER_DIV-rbpoffset],eax  ; Divide by 128

	mov	dword [rbp+APIC_REG_TIMER_LVT-rbpoffset], 0x20020
	mov	dword [rbp+APIC_REG_PERFC_LVT-rbpoffset], 0x10000
	mov	dword [rbp+APIC_REG_LINT0_LVT-rbpoffset], 0x8700
	mov	dword [rbp+APIC_REG_LINT1_LVT-rbpoffset], 0x0400
	mov	dword [rbp+APIC_REG_ERROR_LVT-rbpoffset], 0x10000
	mov	dword [rbp+APIC_REG_APICTIC-rbpoffset],APIC_TICKS

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
	mov	rax, phys_vaddr(pages.gseg_cpu0)
	mov	rdx,rax
	mov	eax,eax
	shr	rdx,32
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
	lea	rax,[rax+160] ; Start on line 2, line 1 has some boot-time debug printouts
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
	add	ax,0x0f00 | '0'
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

	;mov	edi, user_entry_2
	;mov	esi, user_stack_end
	;call	new_proc
	;mov	rdi, rax
	;call	runqueue_append

	;mov	edi, user_entry_3
	;mov	esi, user_stack_end
	;call	new_proc
	;mov	rdi, rax
	;call	runqueue_append

user_stack_end	equ	0x13000

	mov	edi, user_entry
	mov	esi, user_stack_end
	call	new_proc
	; save in non-clobbered register
	mov	rbx, rax
	mov	rdi, rax
	call	runqueue_append

	call	allocate_frame
	mov	rbp, rax
	call	allocate_frame ; TODO Allocate region
	mov	[rax + region.paddr], rbp
	mov	word [rax + region.size], 0x1000

	mov	rdi, [rbx + proc.aspace]
	mov	rsi, rax
	mov	rdx, 0x1234000
	mov	rcx, 0x1000
	call	map_region

	; Replicate the hard-coded read-only region for 0x8000 and 0x9000
	call	allocate_frame ; allocate region, not whole page :)
	mov	edx, 0x8000
	mov	ecx, 0x2000
	mov	[rax + region.paddr], edx
	mov	[rax + region.size], ecx
	mov	rdi, [rbx + proc.aspace]
	mov	rsi, rax
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_X

	mov	rdi, [rbx + proc.aspace]
	zero	esi
	; User stack
	mov	rdx, user_stack_end - 0x1000
	mov	rcx, 0x1000
	call	map_region

	;lea	rdi, [.test_kprintf]
	;mov	rsi, rdi
	;call	printf

	mov	rax, rbx
	jmp	switch_to
;.test_kprintf:
;	db	10, 'Hello, this is the kernel speaking: %p', 10, 0

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
	lea	rbx, [rax - proc]
	mov	[rbx + proc.rip],rsi
	mov	[rbx + proc.rsp],rcx
	; bit 1 of byte 1 of rflags: the IF bit
	; all other bits are set to 0 by default
	mov	byte [rbx + proc.rflags+1], RFLAGS_IF >> 8
	; Copy initial FPU/Media state to process struct
	mov	rsi, [rel globals.initial_fpstate]
	lea	rdi, [rbx + proc.fxsave]
	mov	ecx, 512 / 8
	rep	movsq

	; Allocate address space. Should use kmalloc/slab/etc, aspace is small
	call	allocate_frame
	test	eax, eax
	jz	.oom
	mov	[rbx + proc.aspace], rax

	; Allocate page table
	call	allocate_frame
	test	eax,eax
	jz	.oom

	; Copy a reference to the kernel memory range into the new PML4.
	; Since this currently is at most one 4TB range, this is easy: only a
	; single PML4 entry maps everything by sharing the kernel's lower
	; page tables between all processes.
	mov	esi, [pages.pml4 + 0xff8]
	mov	[rax + 0xff8], esi
	mov	rsi, [rbx + proc.aspace]
	mov	[rsi + aspace.pml4], rax
	sub	rax, phys_vaddr(0)
	mov	[rbx + proc.cr3], rax

	mov	rax,rbx
.oom:
	pop	rbx
.oom_no_pop:
	ret

; rdi: process to add to runqueue
runqueue_append:
	push	rbp
	zero	ebp
	mov	rbp, [gs:ebp + gseg.self]

	mov	rcx, [rbp + gseg.runqueue_last]
	mov	[rbp + gseg.runqueue_last], rdi
	test	rcx,rcx
	jz	.runqueue_empty
	mov	[rcx + proc.next], rdi
	pop	rbp
	ret
.runqueue_empty:
	mov	[rbp + gseg.runqueue], rdi
	pop	rbp
	ret

switch_next:
	mov	rbp, rdi
	mov	r12, rax
	mov	r13, rdx

.gseg_rbp:
%if log_switch_next
lodstr	rdi, 'switch_next', 10
	call	puts
	call	print_procstate
%endif

	mov	rax, r12
	mov	rdx, r13
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
	mov	rcx, [rbp+gseg.runqueue_last]
	mov	[rbp+gseg.runqueue_last], rdx ; runqueue_last = OLD

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
	mov	[rbp+gseg.runqueue], rcx

	; - Load next-process pointer into rax
	zero	ecx
	; OLD->next = NEXT->next = NULL (OLD because it's the tail of the list, NEXT because it's now unlinked)
	mov	[rax+proc.next],rcx ; Clear next-pointer since we're not linked anymore.
	mov	[rdx+proc.next],rcx ; Clear next-pointer since we're not linked anymore.

%if log_switch_next
	mov	rbx, rax
lodstr	rdi, 'switch_next to %p', 10
	mov	rsi, rax
	call	printf
	call	print_procstate
	mov	rax, rbx
%endif

	jmp	switch_to

%ifdef need_print_procstate
; requires rbp = gseg self-pointer
print_procstate:
	push	rbx
lodstr	rdi,	'    Current process: %p', 10
	mov	rsi, [rbp+gseg.process]
	call	printf
lodstr	rdi,	'    Run queue:', 10, 0
	call	puts
	mov	rbx, [rbp+gseg.runqueue]
.loop:
lodstr	rdi,	'        - %p', 10, 0
	mov	rsi, rbx
	call	printf
	test	rbx, rbx
	jz	.end_of_q
	mov	rax, [rbx+proc.next]
	cmp	rax, rbx
	mov	rbx, rax
	je	.error
	jmp	.loop

.error:
lodstr	rdi,	'*** LOOP IN RUNQUEUE: %p points to itself.', 10, 0
	mov	rsi, rbx
	call	printf
	cli
	hlt

.end_of_q:
lodstr	rdi,	'    Runqueue_last: %p', 10, 0
	mov	rsi, [rbp+gseg.runqueue_last]
	call	printf
	pop	rbx
	ret
%endif ;need_print_procstate

; Takes process-pointer in rax, never "returns" to the caller (just jmp to it)
; All registers other than rax will be ignored, trampled, and replaced with the
; new stuff from the switched-to process
switch_to:
	cli	; I don't dare running this thing with interrupts enabled.
	clts	; We'll set it again below, if appropriate

	zero	edi
	mov	rbp, [gs:rdi + gseg.self]

	mov	rbx, rax
%if log_switch_to
lodstr	rdi, 'Switching to %p (cr3=%p).', 10
	mov	rsi, rax
	mov	rdx, [rsi + proc.cr3]
	call	printf
	call	print_procstate
%endif

	; Update pointer to current process
	mov	[rbp+gseg.process], rbx

	; If switching back before anything else uses the FPU, don't set TS
	cmp	rbx, [rbp+gseg.fpu_process]
	je	.no_set_ts
	mov	rax, cr0
	bts	rax, CR0_TS_BIT
	mov	cr0, rax
.no_set_ts:

	; Make sure we don't invalidate the TLB if we don't have to.
	mov	rcx, [rbx + proc.cr3]
	mov	rax, cr3
	cmp	rax, rcx
	je	.no_set_cr3
	mov	cr3, rcx
.no_set_cr3:

	mov	rax, [rbx+proc.flags]
	bt	rax, PROC_KERNEL
	jnc	.user_exit

	; Exit to kernel thread
	; If we don't need to switch rsp this should be easier - restore all
	; regs, rflags, push new rip and do a near return
	push	data64_seg
	push	qword [rbx+proc.rsp]
	push	qword [rbx+proc.rflags]
	push	code64_seg
	jmp	.restore_and_iretq


.user_exit:
	; If we stop disabling interrupts above, this will be wildly unsafe.
	; For now, we rely on the flags-restoring part below to atomically
	; restore flags and go to user mode. The risk is if we switch "from
	; kernel" while having the user GS loaded!
	swapgs
	bt	rax, PROC_FASTRET
	jc	.fast_ret

	bt	qword [rbx+proc.rflags], RFLAGS_IF_BIT
	jnc	.ret_no_intrs

; push cs before this
	; Push stuff for iretq
	push	user_ds
	push	qword [rbx+proc.rsp]
	push	qword [rbx+proc.rflags]
	push	user_cs
.restore_and_iretq:
	push	qword [rbx+proc.rip]
	; rax is first, but we'll take it last...
	lea	rsi, [rbx+proc.rax+8]

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

	mov	rax, rbx
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

struc dlist
	.head	resq 1 ; points to the dlist_node, not to the data
endstruc
struc dlist_node
	.prev	resq 1
	.next	resq 1
endstruc

; A region is a sequential range of pages that can be mapped into various
; address spaces. Not all pages need to be backed by actual frames, e.g. for
; memory-mapped objects. Pages that don't have frames here may have frames in
; the parent region.
struc region
	.parent	resq 1
	.count	resd 1
	.flags	resd 1
	.paddr	resq 1
	.size	resq 1
	; Arbitrary mapping, if there are any
	.mappings restruc dlist
	; Links in the global list of regions
	.node restruc dlist_node
endstruc

REGFLAG_COW	equ 1 ; Hmm, are *mappings* CoW or are *regions*?

; Ordinary and boring flags
MAPFLAG_X	equ 1
MAPFLAG_W	equ 2
MAPFLAG_R	equ 4
MAPFLAG_RWX	equ 7 ; All/any of the R/W/X flags
MAPFLAG_COW	equ 8 ; Hmm, are *mappings* CoW or are *regions*?
MAPFLAG_ANON	equ 16 ; Anonymous page: allocate frame and region on first read.
; "Special" flags are >= 0x10000
MAPFLAG_HANDLE	equ 0x10000

; A non-present page in the page table with this flag set is a handle
PT_FLAG_HANDLE	equ 0x2

; A mapping is a mapped virtual memory range, sometimes backed by a region,
; other times just a placeholder for virtual memory that may become a region by
; allocating/loading-from-disk/whatever the frames that would be required.
struc mapping
	.owner	resq 1 ; address_space
	.region	resq 1
	.kobj	equ .region ; If flags has MAPFLAG_HANDLE, .region points to a kernel object instead
	.flags	resq 1
	; Offset into region that corresponds to the first page mapped here
	.reg_offset resq 1
	.vaddr	resq 1
	.size	resq 1
	; Links for region.mappings
	.reg_node	restruc dlist_node
	; Links for process.mappings
	.as_node	restruc dlist_node
endstruc

struc aspace
	; Upon setup, pml4 is set to a freshly allocated frame that is empty
	; except for the mapping to the kernel memory area (which, as long as
	; it's less than 4TB is only a single entry in the PML4).
	.pml4	resq 1
	; TODO Lock structure for multiprocessing
	.count	resd 1
	.flags	resd 1
	; Do we need a list of processes that share an address space?
	;.procs	resq 1
	.mappings	resq 1
	; Lowest handle used. The range may contain holes. Something clever
	; similar to how we (will) handle noncontiguous mappings will apply :)
	; Also, all handles have mappings.
	.handles_bottom	resq 1
endstruc

section .text

; rdi = dlist
; rsi = dlist_node
; Non-standard calling convention!  Clobbers rdi, r8 but preserves all other
; registers
dlist_prepend:
	mov	r8, [rdi + dlist.head]
	test	r8, r8
	mov	[rdi + dlist.head], rsi
	jz	.empty
	; rsi.next = old head
	mov	[rsi + dlist_node.next], r8
	mov	[r8 + dlist_node.prev], rsi
.empty:
	ret

; rdi: the process (address space!) being mapped into
; rsi: the region or kernel object to map
; rdx: the virtual address to map the region at
; rcx: the size of the virtual mapping
; Returns:
; rax: Newly allocated mapping object
map_region:
	push	rdi
	push	rsi
	push	rdx
	push	rcx
	call	allocate_frame ; TODO mappings are small: kmalloc (or slab)
	pop	rcx
	pop	rdx
	pop	rsi
	pop	rdi
	test	eax,eax
	jz	.oom

	mov	[rax + mapping.owner], rdi
	mov	[rax + mapping.region], rsi
	mov	[rax + mapping.vaddr], rdx
	mov	[rax + mapping.size], rcx

	mov	r9, rdi
	mov	r10, rsi
	lea	rdi, [rdi + aspace.mappings]
	lea	rsi, [rax + mapping.as_node]
	call	dlist_prepend

	test	r10, r10
	jz	.no_region
	lea	rdi, [r10 + region.mappings]
	lea	rsi, [rax + mapping.reg_node]
	call	dlist_prepend

	inc	dword [r10 + region.count]
.no_region:

	mov	rdi, r9
	mov	rsi, r10

.oom:
	ret

; rdi: the address space being mapped into
; rsi: the kernel object being mapped
; (rdx: flags etc. We have up to 11 bits available in the page table depending
; on the minimum alignment of kernel objects. 1 bit is required to set the
; 'present' bit (LSB) to 0, each bit of alignment above 1 is available. For
; 16-byte alignment that means a whopping 3 bits!)
; For starters, all handles point to processes and have no flags.
map_handle:
	test	rsi, 0xf
	jnz	.unaligned_kernel_object

	push	rdi
	push	rsi

	; Calculate the virtual address for the handle
	pop	rdi
	mov	rdx, [rdi + aspace.handles_bottom]
	sub	rdx, 0x1000
	mov	[rdi + aspace.handles_bottom], rdx

	zero	rsi
	zero	ecx ; size is 0
	push	rdx ; vaddr of handle, also return value
	call	map_region
	; pop the kernel object pointer (the rsi we pushed above).
	; MAPFLAG_HANDLE indicates the region pointer is not a region but a
	; kernel object.
	pop	qword [rax + mapping.region]
	mov	byte [rax + mapping.flags + 2], MAPFLAG_HANDLE >> 16
	pop	rax
	ret

.unaligned_kernel_object:
lodstr	rdi,	'Unaligned kobj %p', 10
	call	printf
	cli
	hlt

; rdi: address space to add mapping to
; rsi: physical address of page to add, plus relevant flags as they will appear
; in the page table entry.
; rdx: vaddr to map it to. May be unaligned for convenience.
add_pte:
	push	r12
	push	rsi
	push	rdx

%macro index_table 4 ; source, shift, base, target for address
	mov	rcx, %1
	shr	rcx, %2 - 3
	and	cx, 0xff8 ; Then 'and' away the boring bits
	lea	%4, [%3 + rcx]
%endmacro

%macro do_table 2 ; shift and name
	index_table [rsp], %1, rdi, r12

lodstr	rdi,	'Found ', %2, ' %p at %p', 10
	mov	rsi, [r12]
	mov	rdx, r12
	call	printf

	test	qword [r12], 1
	jnz	%%have_entry

	cmp	qword [r12], 0
	;PANIC: Not present in page table, but it has some data. This shouldn't happen :)
	jnz	.panic

	; No PDP - need to allocate new table
	call	allocate_frame
	lea	rdi, [rax - kernel_base]
	mov	[r12], rdi

lodstr	rdi,	'Allocated ', %2, ' at %p', 10
	mov	rsi, rax
	call	printf

%%have_entry:
	mov	al, [rsp + 8] ; rsp points to low byte of PTE
	or	[r12], al
	mov	rdi, [r12]
	and	di, ~0xfff
	add	rdi, kernel_base
%endmacro

	mov	rdi, [rdi + aspace.pml4] ; Confirm: vaddr of pml4?
	do_table 39, 'PDP'
	do_table 30, 'PD'
	do_table 21, 'PT'
	; r12 now points to PT, add the PTE
	index_table [rsp], 12, rdi, r12
	test	qword [r12], 1
	; We should probably check and handle mapping the same page twice as a no-op
	jnz	.panic

	pop	rdx
	pop	rsi
	mov	[r12], rsi
lodstr	rdi, 'Mapping %p to %p (at %p)!', 10
	mov	rcx, r12
	call	printf
	pop	r12

	ret

.panic:
	cli
	hlt

handler_err:
handler_no_err:
	cli
	hlt

%assign i 0
%rep 18
handler_n %+ i:
	cli
	hlt
	mov	al, i
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
	mov	eax, dword [rel -0x1000 + APIC_REG_APICTCC]
	mov	dword [rdi+gseg.tick], eax
	mov	dword [rel -0x1000 + APIC_REG_APICTIC], APIC_TICKS
	mov	dword [rel -0x1000 + APIC_REG_EOI], 0

	mov	rax,[rdi+gseg.process]
	; The rax and rdi we saved above, store them in process
	pop	qword [rax+proc.rdi]
	pop	qword [rax+proc.rax]
	call	save_from_iret
	mov	rdx, rax
	mov	rax, [rdi+gseg.runqueue]
	jmp	switch_next

handler_NM: ; Device-not-present, fpu/media being used after a task switch
	push	rbp
	push	rdi
	push	rax
	push	rbx
	; FIXME If we get here in kernel mode?
	swapgs

	clts

	; Find the previous process and save fpu/media state to its process struct
	zero	edi
	mov	rbp,[gs:edi + gseg.self]
lodstr	rdi,	'FPU-switch: %p to %p', 10
	mov	rsi,[rbp+gseg.fpu_process]
	mov	rdx,[rbp+gseg.process]
	; FIXME printf may clobber more stuff than what we've actually saved.
	; All caller-save registers must be preserved since we're in an
	; interrupt handler.
	call	printf

	mov	rax,[rbp+gseg.fpu_process]
	test	rax,rax
	; No previous process has unsaved fpu state, just load this process' state
	jz	.no_save_state

	; Save FPU state in process rax
	o64 fxsave [rax+proc.fxsave]
.no_save_state:
	mov	rax,[rbp+gseg.process]
	o64 fxrstor [rax+proc.fxsave]

.ret:
	; FPU state now owned by current process
	mov	[rbp+gseg.fpu_process], rax

	pop	rbx
	pop	rax
	pop	rdi
	pop	rbp
	swapgs
	iretq

handler_PF:
	test	byte [rsp], 0x4
	jz	.kernel_fault

	swapgs
	push	rdi
	push	rbp
	push	rsi
	zero	edi
	mov	rbp, [gs:edi + gseg.self]

	; Page-fault error code:
	; Bit 0: "If this bit is cleared to 0, the page faultwas caused by a not-present page. If this bit is set to 1, the page fault was caused by a page-protection violation.
	; Bit 1: clear = read access, set = write access
	; Bit 2: set = user-space access, cleared = supervisor (tested above)
	; Bit 3: RSV - a reserved bit in a page table was set to 1
	; Bit 4: set = the access was an instruction fetch. Only used when NX is enabled.

lodstr	rdi,	'Page-fault: cr2=%p error=%p', 10, 10
	mov	rsi, cr2
	; Fault
	mov	rdx, [rsp + 24]
	call printf

	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rdi, [rdi + aspace.mappings]
	mov	rsi, cr2
.test_mapping:
	test	rdi, rdi
	jz	.no_match
	mov	rax, [rdi + mapping.vaddr - mapping.as_node]
	cmp	rsi, rax
	je	.found
	jb	.next_mapping
	add	rax, [rdi + mapping.size - mapping.as_node]
	cmp	rsi, rax
	jb	.found
	; vaddr <= cr2 < vaddr+size
.next_mapping:
	mov	rdi, [rdi + dlist_node.next]
	jmp	.test_mapping
.no_match:
lodstr	rdi,	'No mapping found!', 10
	call	printf
.invalid_match:
	cli
	hlt
.found:
	lea	rdx, [rdi - mapping.as_node]
	mov	rcx, [rdi + mapping.vaddr - mapping.as_node]
	mov	r12, rdx
lodstr	rdi,	'Mapping found:', 10, 'cr2=%p map=%p vaddr=%p', 10
	call	printf

	; rsi is fault address (though we already know which mapping we're
	; dealing with)
	mov	rsi, cr2
	mov	rdi, r12 ; Doesn't need offsetting by mapping.as_node anymore

	mov	rax, [rdi + mapping.flags]
	test	eax, MAPFLAG_HANDLE
	jnz	.invalid_match ; Handles can never be accessed as memory
	test	eax, MAPFLAG_RWX
	jz	.invalid_match ; We had none of the read/write/execute permissions

	; Was the access a write access? (bit 1: set == write)
	test	byte [rsp + 24], 0x2
	jz	.not_a_write
	; Write to a read-only page: fault
	test	eax, MAPFLAG_W
	jz	.invalid_match
.not_a_write:

	test	eax, MAPFLAG_ANON
	jz	.map_region

	; Anonymous mapping: allocate a fresh zero page, create a region for
	; it, link the mapping to that region and fall through to adding the
	; new region to the page table.
	cli
	hlt
	mov	ax, __LINE__

	; TODO Check for access-during-instruction-fetch for a present NX page

.map_region
	; 1. The region has a physical page backing it already:
	;   * Check that we are allowed to map it as it is (i.e. correct
	;     permissions, no CoW required, etc)
	;   * Add all required intermediate page table levels
	;   * When reaching the bottom: add a mapping with the right parameters

	mov	rsi, [rdi + mapping.region]
	test	rsi, rsi
	jz	.invalid_match

	mov	r9, cr2
	and	r9w, ~0xfff
	sub	r9, [rdi + mapping.vaddr]
	mov	r8, [rsi + region.paddr]
	add	r8, [rdi + mapping.reg_offset]
	add	r8, r9
	; r8: page frame to add to page table

	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rsi, r8
	mov	rdx, cr2 ; Note: lower 12 bits will be ignored automatically
	; TODO Set the correct flags for the PTE based on the mapping and region
	; flags. Still not sure how to combine region and mapping flags to
	; give what we wnat here :)
	or	rsi, 5 ; [rsi + region.flags]
	call	add_pte

	; TODO Handle failures by killing the process.

.ret:
	; FIXME We've clobbered a whole lot more than this!
	pop	rsi
	pop	rbp
	pop	rdi
	; Pop error code
	add	rsp, 8
	swapgs
	iretq

.no_mappings:
.kernel_fault:
	cli
	hlt
	mov	al,0xe

.message_kp:
	;db 'Oh noes, kernel panic! fault addr %p error %p', 10, 10, 0

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
	cmp	eax, SYSCALL_NEWPROC
	je	.syscall_newproc
	push	rdi
	push	rsi
	push	r11
lodstr	rdi, 'Invalid syscall %p!', 10
	mov	rsi, rax
	call	printf
	pop	r11
	jmp	.sysret

	; Syscall #0: write byte to screen
.syscall_write:
	push	r11
	movzx	edi, dil
	or	di, 0xf00
	call	kputchar
	pop	r11
	jmp	short .sysret

.sysret:
	pop	rcx
.sysret_rcx_set:
	mov	rsp, [gs:gseg.user_rsp]
	bt	r11, RFLAGS_IF_BIT
	jnc	.no_intr
	; Be paranoid and evil - explicitly clear everything that we could have
	; ever clobbered.
	clear_clobbered_syscall

	swapgs
	o64 sysret
.no_intr:
	cli
	hlt
	mov	al,0xff

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

.syscall_newproc:
	zero	ecx
	mov	rcx, [gs:rcx+gseg.self]

	; - Load current process pointer into rax
	mov	rax, [rcx + gseg.process]
	save_regs rbp,rbx,r12,r13,r14,r15
	mov	[rax + proc.rflags], r11
	; See TODO above - the user rsp should be stored directly in the
	; proces struct.
	mov	rdx, [rcx+gseg.user_rsp]
	mov	[rax+proc.rsp], rdx
	; Save away the current process pointer in a callee-save reg
	mov	rbp, rax

	; TODO: Validate some stuff
	; rdi: entry point
	; rsi: stack pointer of new process
	call	new_proc
	mov	rdi,rax
	mov	rbx,rax ; Save new process id
	; FIXME How should we decide which address space to use for the new
	; process?
	;lea	rax, [phys_vaddr(aspace_test)]
	;mov	[rbx + proc.aspace], rax
	;mov	rsi, [rbp + proc.aspace]
	;call	add_process_to_aspace
	;call	rdi, rbx
	call	runqueue_append

	mov	rdi,rbp ; Process. Should be address space?
	mov	rsi,rbx ; Handle of new process that should be mapped
	call	map_handle
	mov	r12, rax

lodstr	rdi, '+NEWPROC %p at %p', 10
	mov	rsi, rbx
	mov	rcx, rax
	call	printf
	zero	ebp
	mov	rbp, [gs:rbp + gseg.self]
	;call	print_procstate
;lodstr rdi, '-NEWPROC', 10
;	call	printf

	mov	rax, r12

	; TODO Restore callee-save registers from process struct, clear
	; caller-save registers to avoid leaking kernel details.

	mov	r11, [rbp + gseg.process]
	mov	r11, [r11 + proc.rflags]
	jmp	.sysret

section usermode

user_entry_new:
	; TODO Define the initial program state for a process.
	; - parent pid
	; - function parameters in registers?

lodstr	rdi, 'user_entry_new', 10
	call	printf

	mov	eax, SYSCALL_RECV
	syscall

	mov	rsi, rdi
lodstr	rdi, 'received %p', 10
	mov	edi, msg_resperr(MSG_USER)
	mov	eax, SYSCALL_SEND
	syscall

	jmp	user_entry_new


user_entry:

	;mov	rdx, 0x1234123
	;mov	[rdx], edx

	mov	edi, user_entry_new
	mov	rsi, rsp
	mov	eax, SYSCALL_NEWPROC
	syscall

	mov	rbx, rax

lodstr	rdi, 'newproc: %p', 10
	mov	rsi, rax
	call	printf

	mov	dword [rbx], 0

	mov	rsi, rbx
	mov	eax, SYSCALL_SENDRCV
	mov	edi, msg_sendcode(MSG_USER)
	syscall

	jmp	$

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
	movq	xmm1, rbx
	movq	xmm0, xmm1
.start:
	mov	ebx, 7
.loop
	lea	edi,['a'+ebx]
	xor	eax,eax
	syscall

	paddq	xmm0, xmm1
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

	jmp	.start

.test_message:
	db	'Hello World from puts',10,0
.test_format:
	db	'printf %% "%s" %c',10,0
.test_arg1:
	db	'Hello World',0

puts:
	; callee-save: rbp, rbx, r12-r15
	push	rbp
	mov	rbp,rdi

.loop:
	mov	dil,[rbp]
	test	dil,dil
	jz	.ret
	inc	rbp

	call	putchar
	jmp	.loop

.ret:
	pop	rbp
	clear_clobbered
	ret

; edi: character to put
putchar:
	mov	eax, cs
	lar	eax, ax
	test	eax, 3 << 13
	jz	.kputchar

	mov	eax, SYSCALL_WRITE
	syscall
	clear_clobbered
	ret
.kputchar:
	mov	edx, edi
	test	dh, dh
	jnz	.set
	mov	dh, 0x1f
	mov	edi, edx
.set:
	;jmp	kputchar

kputchar:
	push	rbp
	zero	eax
	mov	rbp, [gs:eax + gseg.self]
.have_rbp:
	mov	eax, edi
	mov	rdi, [rbp+gseg.vga_pos]
	cmp	al,10
	je	.newline

	stosw

.finish_write:
	cmp	rdi, [rbp+gseg.vga_end]
	cmovge	rdi, [rbp+gseg.vga_base]
	mov	[rbp+gseg.vga_pos], rdi
	clear_clobbered
	pop	rbp
	ret

.newline:
	mov	esi, eax
	mov	rax, rdi
	sub	rax, [rbp + gseg.vga_base] ; Result fits in 16 bits.
	cwd ; ax -> dx:ax
	mov	ecx,160
	div	cx
	; dx now has the remainder
	sub	cx,dx
	shr	cx,1
	mov	eax,esi
	mov	al,'-'
	rep	stosw

	jmp	.finish_write

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

	push	r12
	push	r13
	push	rbx

.nextchar:
	lodsb
	test	al,al
	jz	.done
	cmp	al,'%'
	je	.handle_format

.write_al:
	mov	r12,rdi
	mov	r13,rsi
	movzx	edi,al
	call	putchar
	mov	rsi,r13
	mov	rdi,r12
	jmp	.nextchar

.handle_format:
	lodsb
	cmp	al,'c'
	je	.fmt_c
	cmp	al,'s'
	je	.fmt_s
	cmp	al,'p'
	je	.fmt_p
	;cmp	al,'%'
	jmp	.write_al

.fmt_c:
	mov	rax,[rdi]
	add	rdi,8
	jmp	.write_al

.fmt_s:
	; syscall will clobber rsi and rdi but not r12 and r13
	lea	r13,[rdi+8]
	mov	r12,rsi

	mov	rdi,[rdi]
	call	puts

	mov	rsi,r12
	mov	rdi,r13
	jmp	.nextchar

.fmt_p:
	lea	r12,[rdi+8]
	mov	r13,rsi
	mov	rbx, [rdi]

	mov	edi, '0'
	call	putchar
	mov	edi, 'x'
	call	putchar

	mov	cl, 64

	lea	r14, [rel digits]
.loop:
	sub	cl, 4
	mov	rdi, rbx
	shr	rdi, cl
	and	edi, 0xf
	mov	dil, byte [r14 + rdi]
	mov	bpl, cl
	call	putchar
	mov	cl, bpl
	test	cl, cl
	jnz	.loop

	mov	rsi,r13
	mov	rdi,r12
	jmp	.nextchar

.done:
	pop	rbx
	pop	r13
	pop	r12
	clear_clobbered
	add	rsp,48
	jmp	[rsp-48]

section .data

message:
	dq 0x0747074e074f074c, 0x07450744074f074d
digits:
	db '0123456789abcdef'

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
	define_tss64 0x68, text_vpaddr(tss)
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
	dq	text_vpaddr(gdt_start)  ; Offset

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
	%rep 4
	default_error ; 10-13
	%endrep
	interrupt_gate handler_PF ; 14, #PF
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
	dq	text_vpaddr(idt)

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

