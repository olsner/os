[map all kstart.map]

; Configuration
; NB: Enable too many of these and the kernel pops too close to 8kB and stops
; working.
%define log_switch_to 0
%define log_switch_next 0
%define log_idle 0
%define log_runqueue 0
%define log_runqueue_panic 0
%define log_fpu_switch 0 ; Note: may clobber user registers if activated :)
%define log_timer_interrupt 0
%define log_page_fault 0
%define log_find_mapping 0
%define log_find_backing 0
%define log_unmap 0
%define log_find_handle 0
%define log_new_handle 0
%define log_hmod 0
%define log_find_senders 0
%define log_mappings 0
%define log_add_pte 0
%define log_waiters 0
%define log_messages 0
%define log_irq 0
%define log_alloc 0
%define log_pulses 0
%define verbose_procstate 0
%assign need_print_procstate (log_switch_to | log_switch_next | log_runqueue | log_runqueue_panic | log_waiters | log_find_senders | log_timer_interrupt)

; print each word of the mbootinfo block on bootup
%define log_mbi 0
%define bochs_con 1

%define debug_tcalls 0

; Use an unrolled loop of movntdq (MMX?) instructions to clear pages
%define unroll_memset_0 0

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

%define PANIC PANIC_ __LINE__
%macro PANIC_ 1
	mov	esi, %1
	tcall	panic_print
%assign need_panic 1
%endmacro

%macro tcall 1
%if debug_tcalls
	call	%1
	ret
%else
	jmp	%1
%endif
%endmacro

; "tc nz, foo" does the same as jnz foo (but also includes breadcrumbs in debug
; builds)
%macro tc 2
%if debug_tcalls
	j%-1	%%skip
	tcall	%2
%%skip:
%else
	j%+1	%2
%endif
%endmacro

%include "macros.inc"
%include "msr.inc"
%include "proc.inc"
%include "segments.inc"
%include "string.inc"
%include "mboot.inc"
%include "pages.inc"
%include "syscalls.inc"
%include "messages.inc"
%include "pic.inc"

RFLAGS_IF_BIT	equ	9
RFLAGS_IF	equ	(1 << RFLAGS_IF_BIT)
RFLAGS_VM	equ	(1 << 17)

APIC_TICKS	equ	10000
APIC_PBASE	equ	0xfee00000

kernel_base equ -(1 << 30)

; Per-CPU data (theoretically)
struc	gseg
	; Pointer to self
	.self	resq	1
	; Kernel stack pointer for syscalls and interrupts
	.rsp	resq	1
	.vga_base	resq 1
	.vga_pos	resq 1
	.vga_end	resq 1

	.curtime	resd 1
	.tick		resd 1

	.process	resq 1
	.runqueue	restruc dlist

	; Process last in use of the floating point registers
	.fpu_process	resq 1
	; Process that should receive IRQ interrupts
	.irq_process	resq 1
	; bitmask of irqs that have been delayed
	; (also add per-irq counter?)
	.irq_delayed	resq 1

	.free_frame	resq 1
	.temp_xmm0	resq 2
endstruc

section .text vstart=pages.kernel
text_vstart_dummy:
section .rodata vfollows=.text follows=.text align=4
rodata_vstart_dummy:
section bss nobits align=8 vfollows=.rodata
bss_vstart_dummy:

; get the physical address of a symbol in the .text section
%define text_paddr(sym) (section..text.vstart + sym - text_vstart_dummy)
%define bss_paddr(sym) (section.bss.vstart + sym - bss_vstart_dummy)
%define rodata_paddr(sym) (section..rodata.vstart + sym - rodata_vstart_dummy)
; get the virtual (kernel) address for a symbol in the .text section
%define text_vpaddr(sym) phys_vaddr(text_paddr(sym))
; get the virtual (kernel) address for a .bss-section symbol
%define bss_vpaddr(sym) phys_vaddr(bss_paddr(sym))
; translate a physical address to a virtual address in the 'core'
; Note: in most cases, RIP-relative addressing is enough (since we tell the
; assembler we're based at 0x8000 while we're actually at kernel_base+0x8000),
; but this can be used for constant data or wherever we need the full address.
%define phys_vaddr(phys) kernel_base + phys

; Note: Must all be in the same section, otherwise trouble with complicated
; expressions that rely on bitwise arithmetic on symbol relocations
section .text

%include "start32.inc"

align 4
mboot_header:
mboot MBOOT_FLAG_LOADINFO | MBOOT_FLAG_NEED_MEMMAP
	; | MBOOT_FLAG_NEED_VIDMODE
mboot_load \
	text_paddr(mboot_header), \
	section..text.vstart, \
	section.data.end, \
	kernel_reserved_end, \
	text_paddr(start32_mboot)
;mboot_vidmode_text
endmboot

bits 64
default rel

%define printf kprintf
%define puts kputs
%define putchar kputchar

;;
; The main 64-bit entry point. At this point, we have set up a page table that
; identity-maps 0x8000 and 0x9000 and maps all physical memory at kernel_base.
;;
start64:
	; Start by jumping into the kernel memory area at -1GB. Since that's a
	; 64-bit address, we must do it in long mode...
	jmp	phys_vaddr(.moved)
.moved:
	; Need to reload gdtr since it has a 32-bit address (vaddr==paddr) that
	; will get unmapped as soon as we leave the boot code.
	lgdt	[gdtr]
	; Load before setup :)
	lidt	[idtr]

init_idt:
	lea	rbp, [rel idt]
	lea	rsi, [rel idt_data.vectors]
	mov	ecx, (idt_data.vectors_end - idt_data) / 3
.loop:
	zero	eax
	; Vector
	lodsb
	shl	eax, 4 ; 16 bytes per entry
	lea	rdi, [rbp + rax]
	; Offset from vstart
	lodsw
	; upper part of address: always 0xffffffff
	add	eax, (kernel_base + pages.kernel) & 0xffff
	; eax = lower address
	stosw
	mov	eax, cs
	stosw
	mov	eax, ((kernel_base + pages.kernel) & (0xffff << 16)) | ((GATE_PRESENT | GATE_TYPE_INTERRUPT) << 8)
	stosd
	dec	dword [rdi]
	loop	.loop

load_idt:

	mov	rdi,phys_vaddr(0xb8004)
	lea	rsi,[rel message]
	mov	ecx, 4
	rep movsd

	mov	rsp, phys_vaddr(kernel_stack_end)
	mov	ax,tss64_seg
	ltr	ax

init_frames:
	; pointer to last page linked, will be stored later on in garbage-frame list

	; r8: kernel_base
	; r9d: first page to actually use, reserving:
	; 0..0x8000 for null pointers and waste,
	; 0x8000..0x13000 for kernel crap,
	; 0x13000..0x100000 because it contains fiddly legacy stuff
	; 0x100000..[memory_start] contains multiboot modules and other data
	mov	r8, phys_vaddr(0)
	mov	r9d, [memory_start]
	zero	r10

	mov	r11, phys_vaddr(0xb8020)

	mov	esi, [mbi_pointer]
	add	rsi, r8
	;mov	rsi, phys_vaddr(pages.memory_map)
	mov	ebx, [rsi + mbootinfo.mmap_length]
	mov	esi, [rsi + mbootinfo.mmap_addr]
	add	rsi, r8
	add	rbx, rsi
	; The range of memory map info is now in rsi..rbx
.loop:
	cmp	rbx, rsi
	jbe	.done
	lodsd ; size of entry
	lea	r12, [rsi + rax] ; pointer to next entry
	lodsq
	mov	rdi, rax ; start of range
	lodsq
	mov	rdx, rax ; length of range
	lodsd
	mov	rsi, r12 ; update rsi
	cmp	eax, E820_MEM
	; TODO ranges with value 3 (E820_ACPI_RECLAIM) are reusable once we're done with the ACPI data in them
	jne	.loop
	add	ax,0x0f00 | '0'
	mov	word [r11], ax
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

cleanup_pages:
	; Clear out the page tables we're going to reuse
	zero	eax
	lea	rdi, [pages.low_pd]
	mov	ecx, 512
	rep	stosd

	; Clear old link that points to low_pdp (where these mappings will
	; otherwise be duplicated)
	mov	[pages.pml4], eax
	mov	[pages.kernel_pdp + 0xff0], dword pages.low_pd | 3
	mov	[pages.low_pd + 0xff8], dword pages.low_pt | 3

; This has nothing to do with the APIC
apic_setup:
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
	bts	eax, 11 ; Set NXE
	wrmsr

	; This is the kernel GS (it is in fact global, but it should be the per-cpu thingy)
	mov	rax, phys_vaddr(pages.gseg_cpu0)
	mov	rdx,rax
	mov	rsi, rax
	mov	eax,eax
	shr	rdx,32
	mov	ecx, MSR_GSBASE
	wrmsr

	; after this, ebx should be address to video memory and rdi points to
	; the gs-segment data block. So does rbp.
	mov	rdi, rsi
	mov	rbp, rdi
	zero	eax
	mov	ecx, 4096/4
	rep	stosd

	mov	rax, rsi
	mov	rdi, rsi
	stosq ; gs:0 - selfpointer
	mov	rax, rsp
	stosq
	mov	rax,phys_vaddr(0xb8000)
	;mov	eax,0xb8000
	stosq ; gs:8 - VGA buffer base
	lea	rax,[rax+160] ; Start on line 2, line 1 has some boot-time debug printouts
	stosq ; gs:16 - VGA writing position
	lea	rax,[rax+80*25*2-160]
	stosq ; gs:24 - VGA buffer end

E820_MEM	equ 1
E820_RESERVED	equ 2
E820_ACPI_RCL	equ 3
; There is also 4, which is some ACPI thingy that we shouldn't touch

fpu_initstate:
	call	allocate_frame
	o64 fxsave [rax]
	mov	[rel globals.initial_fpstate], rax

	; Make the first use of fpu/multimedia instructions cause an exception
	mov	rax,cr0
	bts	rax,CR0_TS_BIT
	mov	cr0,rax

%if log_mbi
show_mbi_info:
	mov	esi, dword [mbi_pointer]
	add	rsi, phys_vaddr(0)
	mov	rbx, rsi
	mov	ecx, mbootinfo_size / 4
.loop:
	lodsd
	push	rcx
	push	rsi

lodstr	rdi, 'mboot %x: %x', 10
	sub	rsi, rbx
	sub	esi, 4
	mov	edx, eax
	call	printf

	pop	rsi
	pop	rcx
	loop	.loop
%endif

list_mbi_modules:
	mov	rbx, phys_vaddr(0)
	mov	r12d, [mbi_pointer]
	add	r12, rbx
	zero	r13

lodstr	rdi,	'%x modules loaded, table at %p', 10
	mov	esi, [r12 + mbootinfo.mods_count]
	mov	edx, [r12 + mbootinfo.mods_addr]
	add	rdx, rbx
	call	printf

	mov	ecx, [r12 + mbootinfo.mods_count]
	mov	esi, [r12 + mbootinfo.mods_addr]
	add	rsi, rbx

	jrcxz	.done
.loop:
	push	rsi
	push	rcx
lodstr	rdi,	'Module at %p size %x', 10 ; : %s', 10, '%s', 10
	mov	edx, [rsi + mboot_mod.end]
	mov	r8d, [rsi + mboot_mod.string]
	mov	esi, [rsi + mboot_mod.start]
	; save stuff away for launch_user
	mov	r13, rsi ; start of module
	mov	r14, rdx ; end of module
	; edx = length
	sub	edx, esi
	; rbx = kernel_base, r8 = string, rsi = start
	lea	rcx, [rbx + r8]
	lea	r8, [rbx + rsi]
	mov	byte [r8 + rdx], 0
	; r8 = start, rcx = string
	mov	rsi, r8
	push	rcx
	; rsi = start
	; rdx = mod_length
	; rcx = string
	; r8 = start
	call	printf
	pop	r8
	pop	rcx
	pop	rsi
	add	rsi, mboot_mod_size
	push	rsi
	push	rcx
	push	r8
	call	launch_user
	push	rax
lodstr	rdi, '=> process %p', 10
	mov	rsi, rax
	call	printf
	pop	rax
	pop	r8
	pop	rcx
	pop	rsi
	push	rax ; Superfluous push, to add the created process to a list
	loop	.loop
.done:

	; We now have a list of processes at rsp..rsp+8*n, connect them to each
	; other somehow.
	;
	; Perhaps set their initial register states to something nice like
	; al = the number of processes in boot package, ah = index of self
	; Where handles 1..al (except ah) are the handles to those processes
	; It's up to each one to rename to something suitable or delete the
	; handles. (And to have an idea of which handle goes to what.)

	; Let's assume the stack was empty and page-aligned before that. (We
	; could also push a 0 at the start (end) as a sentinel.)
	mov	ecx, esp
	neg	ecx
	and	ecx, 0xfff
	shr	ecx, 3
	jrcxz	initial_handles.done
initial_handles:
	; cx = n, [rsp] = process n
	; for each process from i = n-1 down to 1, map n <-> i
	pop	rdi
	mov	rsi, rsp
	push	rcx
.inner:
	dec	ecx
	jz	.inner_done

	; rsi points to the next process to map
	; rdi is process n (constant)
	; [rsp] is old ecx (n)
	; ecx goes from n-1 to 1
	;
	; we want to map rdi:ecx <-> [rsp]:[rsi++]
	lodsq
	mov	rdx, [rsp]
	push	rsi
	push	rdi
	mov	rsi, rdi
	mov	rdi, rax
	push	rcx

	call	proc_assoc_handles
	pop	rcx
	pop	rdi
	pop	rsi
	jmp	.inner

.inner_done:
	pop	rcx
	loop	initial_handles

.done:
	; Set up the pointer to the dedicated IRQ process
	mov	[rbp + gseg.irq_process], rdi
	mov	rsi, rdi
lodstr	rdi,	'done. first process is %p', 10
	call	printf

	jmp	switch_next

handle_irq_generic:
	; we have an irq number on stack
	; all other state undefined
	push	rax
	push	rbp
	swapgs

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
	add	rax, [rbp + gseg.process]
	jz	.no_save
	; The rax and rbp we saved above, store them in process
	pop	qword [rax + proc.rbp]
	pop	qword [rax + proc.rax]
	call	save_from_iret

	mov	rdx, [rbp + gseg.process]
	pop	rbx
	pop	rbx ; interrupt number

%if log_irq
lodstr	rdi,	'handle_irq_generic(%x): proc=%p', 10
	mov	rsi, rbx
	call	printf
%endif

	; some kind of temporary code - do we need to clear gseg.process to 0?
	zero	edi
	xchg	rdi, [rbp + gseg.process]
	call	runqueue_append
	jmp	.saved
.no_save:
	mov	rbx, [rsp + 24] ; rbp rax rip irq(was:cs)
%if log_irq
lodstr	rdi,	'handle_irq_generic(%x): idle', 10
	mov	rsi, rbx
	call	printf
%endif

	; Pop the saved rax/rbp, plus the 5-word interrupt stack frame
	add	rsp, 7 * 8
.saved:

%if log_irq
lodstr	rdi,	'handle_irq_generic: irq-proc=%p (%x)', 10
	mov	rsi, [rbp + gseg.irq_process]
	mov	edx, [rsi + proc.flags]
	call	printf
%endif

	; Perhaps this could be "process-global pulse" instead - removing the
	; global from gseg. (More words per process though, and IRQs are not
	; handled by most processes!)
	sub	ebx, 32
	; FIXME bounds check
	bts	dword [rbp + gseg.irq_delayed], ebx
	; The IRQ was already received and delayed - no need to do anything
	jc	.delay

	; Not very nice to duplicate message-sending here...
	; Double not-nice to duplicate the code from _recv to handle an IRQ
	; that was delayed.
	; And to duplicate what should be possible to handle through the pulse
	; API. If there's a handle associated with the IRQ receipt, that handle
	; could be put in the process' pending-handle list instead.
	mov	rax, [rbp + gseg.irq_process]
	test	[rax + proc.flags], byte PROC_IN_SEND
	jnz	.delay
	test	[rax + proc.flags], byte PROC_IN_RECV
	jz	.delay
	mov	rsi, [rax + proc.rdi]
	test	rsi, rsi
	jz	.recv_from_any
	cmp	qword [rsi + handle.other], 0
	jnz	.delay
	push	rax
	mov	rdi, rsi
	call	free_frame
	pop	rax
.recv_from_any:

	and	[rax + proc.flags], byte ~PROC_IN_RECV
	zero	esi
	xchg	rsi, [rbp + gseg.irq_delayed]
	mov	[rax + proc.rdi], rsi
	mov	qword [rax + proc.rax], MSG_PULSE

	jmp	switch_to
.delay:
%if log_irq
lodstr	rdi,	'handle_irq_generic(%x): delivery delayed (%x)', 10
	mov	rsi, rbx
	mov	rdx, [rbp + gseg.irq_delayed]
	call	printf
%endif

	jmp	switch_next

%macro handle_irqN_generic 1
handle_irq_ %+ %1:
	; Overwrite CS. We don't let user programs change their CS.
	mov	[rsp + 8], byte %1
	jmp	handle_irq_generic
%endmacro

%assign irq 32
%rep 17
handle_irqN_generic irq
%assign irq irq + 1
%endrep

; rdi = process A
; rsi = process B
; rdx = handle name in A
; rcx = handle name in B
proc_assoc_handles:
	push	rcx ; handle name in B (not used yet)
	push	rdi ; procA
	push	rsi ; procB

%if 0
	push	rdx
	; di si dx cx --> di dx cx si --> si dx cx 8
	mov	r8, rsi
	mov	rsi, rdi
lodstr	rdi,	'%p:%x <-> %x:%p', 10
	call	printf

	pop	rdx
	mov	rsi, [rsp]
	mov	rdi, [rsp + 8]
	mov	ecx, [rsp + 16]
%endif

	mov	rdi, [rdi + proc.aspace]
	xchg	rsi, rdx
	; rdi = aspace
	; rsi = handle name in A (we don't need this anymore)
	; rdx = process B
	call	map_handle
	; rax = handleA

	; stack: processB processA handleNameB
	pop	rdi
	mov	rdi, [rdi + proc.aspace]
	pop	rdx
	pop	rsi
	; rax: the handle object we just mapped
	push	rax
	call	map_handle
	; stack: handleA
	pop	rdx
	; rax = handleB
	; rdx = handleA
	mov	[rax + handle.other], rdx
	mov	[rdx + handle.other], rax
	ret

; Set up a new process with some "sane" defaults.
;
; rdi: entry point and first page of process
; rsi: end of module
new_proc_simple:
	push	rbx
	; TODO Save more registers that we clobber below.

	; Round up end-of-module
	add	rsi, 0xfff
	and	si, 0xf000
	mov	r12, rdi ; FIXME r12,r13 = callee-save?
	mov	r13, rsi
	; Round down entry-point/start-of-module
	and	r12w, 0xf000

	; rdi = entry point (preserved from in parameter)
	; rsi = top of stack, we put it just below the start-of-module
	mov	esi, 0x100000
	and	edi, 0xfff
	add	edi, esi
	call	new_proc
	mov	rbx, rax ; save away created process in callee-save register

	; Memory map for new process:
	; 1MB - 4k: (stack)
	; anon, RW (no X)
	; 1MB..end: (module/"text")
	; phys, RX, -> paddr (r12) - 1MB (*offset*)
	; end: null
	; (three "cards")
	; for a direct physical mapping, paddr = .vaddr + .offset
	; (vaddr = 1MB, paddr = r12, offset = r12 - 1MB)

	; r12 = phys start
	; r13 = phys end
	sub	r13, r12
	mov	eax, (1 << 20)
	add	r13, rax
	; r13 = 1MB + length (= vaddr of end)
	push	r13
	push	0 ; end-vaddr -> no access

	; 1 MB -> paddr
	push	rax
	; r12 = offset from 1MB to phys start (= paddr - 1MB)
	sub	r12, rax
	or	r12, MAPFLAG_PHYS | MAPFLAG_R | MAPFLAG_X
	push	r12
	mov	eax, (1 << 20) - 4096
	push	rax
	; null offset, anon, RW
	push	byte MAPFLAG_ANON | MAPFLAG_R | MAPFLAG_W

	; stack: in pop order: offset, then vaddr of things to map
.map:
	zero	ecx ; handle = 0
	pop	rdx ; offset + access
	; r12: save so we can check later if we've reached the end
	mov	r12, rdx
	pop	rsi ; rsi = vaddr
	mov	rdi, [rbx + proc.aspace]
; rdi: aspace
; rsi: vaddr
; rdx: offset + flags
; rcx: handle
	call	mapcard_set
	test	r12, r12
	jnz	.map

	mov	rax, rbx
	pop	rbx
	ret

launch_user:
lodstr	rdi,	'Loading module %p..%p', 10
	mov	rsi, r13
	mov	rdx, r14
	call	printf

	mov	rdi, r13
	mov	rsi, r14
	call	new_proc_simple

	push	rax
	mov	rdi, rax
	call	runqueue_append
	pop	rax
	ret

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

; rdi: process being waited for
; rsi: process that should start waiting
add_to_waiters:
	push	rdi
	push	rsi
%if log_waiters
	mov	rdx, [rsi + proc.flags]
	mov	r8, [rdi + proc.flags]
	mov	rcx, rdi
lodstr	rdi, 'Blocking %p (%x) on %p (%x)', 10
	call	printf
lodstr rdi, 'Blocked %p (%x)', 10
	mov	rsi, [rsp]
	call	print_proc
lodstr rdi, 'Waiting for %p (%x)', 10
	mov	rsi, [rsp+8]
	call	print_proc

	mov	rsi, [rsp]
	mov	rdi, [rsp + 8]
%endif

	test	[rsi + proc.flags], byte PROC_ON_RUNQUEUE
	jnz	.on_runqueue
	cmp	[rsi + proc.waiting_for], rdi
	je	.already_waiting
	; rsi will become waiting for rdi, check that rdi was not already
	; waiting for rsi (that implies a deadlock)
	cmp	[rdi + proc.waiting_for], rsi
	je	.deadlock

	mov	[rsi + proc.waiting_for], rdi
	add	rdi, proc.waiters
	add	rsi, proc.node
	call	dlist_append

.already_waiting:
%if log_waiters
lodstr	rdi,	'Blocked process %p (%x)', 10
	mov	rsi, [rsp]
	call	print_proc
lodstr	rdi,	'Waiting for %p (%x)', 10
	mov	rsi, [rsp + 8]
	call	print_proc
%endif

	pop	rsi
	pop	rdi
	ret

.deadlock:
	; Kill both processes and clean up after them.
	PANIC
.on_runqueue:
	PANIC

; rdi: process that was being waited for
; rsi: process that might need waking up
stop_waiting:
	push	rsi
%if log_waiters
	push	rdi
	mov	rcx, rdi
lodstr	rdi, 'Unblocking %p (%x) from %p (%x)', 10
	mov	rdx, [rsi + proc.flags]
	mov	r8, [rcx + proc.flags]
	call	printf
lodstr rdi, 'Unblocked %p (%x)', 10
	mov	rsi, [rsp + 8]
	call	print_proc
	pop	rdi
	mov	rsi, [rsp]
%endif

	cmp	[rsi + proc.waiting_for], rdi
	jne	.not_waiting

	zero	eax
	mov	[rsi + proc.waiting_for], rax
	add	rdi, proc.waiters
	add	rsi, proc.node
	call	dlist_remove

.not_waiting:
%if log_waiters
	mov	rdi, [rsp]
	call	runqueue_append
lodstr	rdi,	'Unblocked process %p (%x)', 10
	mov	rsi, [rsp]
	call	print_proc
lodstr	rdi,	'<end>', 10
	call	puts
	pop	rdi
	ret
%else
	pop	rdi
	tcall	runqueue_append
%endif

; rdi: process to add to runqueue
runqueue_append:
%if log_runqueue
	push	rdi
	mov	rsi, rdi
lodstr	rdi, 'runqueue_append %p', 10
	call	printf
	pop	rdi
%endif
	test	[rdi + proc.flags], byte PROC_RUNNING
	jnz	.already_running
	test	[rdi + proc.flags], byte PROC_IN_SEND | PROC_IN_RECV
	jne	.queueing_blocked
	bts	dword [rdi + proc.flags], PROC_ON_RUNQUEUE_BIT
	jc	.ret ; already on runqueue
	lea	rsi, [rdi + proc.node]
	lea	rdi, [rbp + gseg.runqueue]
	call	dlist_append
.ret:
%if log_runqueue && verbose_procstate
	call	print_procstate
%endif
	ret
.queueing_blocked:
%if verbose_procstate
	mov	rsi, rdi
lodstr	rdi, 'queueing blocked %p (%x)', 10
	call	print_proc
	call	print_procstate
%endif
	PANIC
.already_running:
%if verbose_procstate
	mov	rsi, rdi
lodstr	rdi, 'queueing already running %p (%x)', 10
.panic:
	call	print_proc
	call	print_procstate
%endif
	PANIC

runqueue_pop:
	lea	rdi, [rbp + gseg.runqueue]
	call	dlist_pop
	test	rax, rax
	jz	.empty
	sub	rax, proc.node
	btr	dword [rax + proc.flags], PROC_ON_RUNQUEUE_BIT
.empty:
	ret

idle:
%if log_idle
lodstr	rdi, 'Idle: proc=%p', 10
	mov	rsi, [rbp + gseg.process]
	call	printf
%endif
	zero	eax
	mov	[rbp + gseg.process], rax
	; Fun things to do while idle: check how soon the first timer wants to
	; run, make sure the APIC timer doesn't trigger before then.
	swapgs
	sti
	hlt
	; We should never get here: the interrupt handler(s) will return through
	; the scheduler which will just re-idle if needed.
	; It might be possible for hlt to fall through without an interrupt
	; happening. It looks like the SMM code ran after an SMI can decide
	; whether to resume or abort the hlt instruction.
%if log_idle
lodstr	rdi,	'Idle: hlt fell through?', 10
	call	printf
%endif
	jmp idle

block_and_switch:
	btr	dword [rdi + proc.flags], PROC_RUNNING_BIT
	jnc	switch_next
	zero	edi
	mov	[rbp + gseg.process], rdi

%if log_waiters
	mov	rsi, rdi
lodstr rdi, 'Blocked %p (%x)', 10
	call	print_proc
%endif


switch_next:
%if log_switch_next
lodstr	rdi, 'switch_next', 10
	call	printf
%if verbose_procstate
	call	print_procstate
%endif
%endif

	call	runqueue_pop
	test	rax, rax
	jz	idle
	tcall	switch_to

%if verbose_procstate
print_proc:
	; rdi = format
	; rsi = process
	zero	edx
	test	rsi, rsi
	jz	.no_proc
	mov	rdx, [rsi + proc.flags]
	push	rsi
	call	printf
; As long as we only ever print runnable processes...
	mov	rsi, [rsp]
	mov	rsi, [rsi + proc.waiting_for]
	test	rsi, rsi
	jz	.not_waiting
lodstr	rdi,	'     Waiting for %p', 10
	call	printf
.not_waiting:
	mov	rsi, [rsp]
	cmp	qword [rsi + proc.waiters + dlist.head], 0
	jz	.no_waiters
lodstr	rdi,	'     Waiters:', 10
	call	printf
	pop	rsi
	mov	rsi, [rsi + proc.waiters + dlist.head]
.waiter_loop:
	sub	rsi, proc.node
	push	rsi
lodstr	rdi,	'     - %p (%x)', 10
	mov	rdx, [rsi + proc.flags]
	call	printf
	pop	rsi
	mov	rsi, [rsi + proc.node + dlist_node.next]
	test	rsi, rsi
	jnz	.waiter_loop
	ret
.no_waiters:
	pop	rsi
.no_proc:
	ret

; requires rbp = gseg self-pointer
print_procstate:
	push	rbx
lodstr	rdi,	'    Current process: %p (%x)', 10
	mov	rsi, [rbp + gseg.process]
	call	print_proc
lodstr	rdi,	'    Run queue:', 10, 0
	call	printf
	mov	rbx, [rbp + gseg.runqueue + dlist.head]
.loop:
	test	rbx, rbx
	jz	.end_of_q
lodstr	rdi,	'    - %p (%x)', 10, 0
	lea	rsi, [rbx - proc.node]
	call	print_proc
	mov	rax, [rbx + dlist_node.next]
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
	mov	rsi, [rbp + gseg.runqueue + dlist.tail]
	test	rsi, rsi
	jz	.no_eoq
	sub	rsi, proc.node
.no_eoq:
	call	printf
	pop	rbx
	ret
%elif need_print_procstate
print_proc:
	; rdi = format
	; rsi = process
	zero	edx
	test	rsi, rsi
	jz	.no_proc
	mov	rdx, [rsi + proc.flags]
.no_proc:
	tcall	printf
%endif ; verbose_procstate

; Takes process-pointer in rax, never "returns" to the caller (just jmp to it)
; Requires gseg-pointer in rbp
; All registers other than rax will be ignored, trampled, and replaced with the
; new stuff from the switched-to process
switch_to:
%if log_switch_to
	mov	rbx, rax
lodstr	rdi, 'Switching to %p (%x, cr3=%x, rip=%x) from %p', 10
	mov	rsi, rax
	mov	rdx, [rsi + proc.flags]
	mov	rcx, [rsi + proc.cr3]
	mov	r8, [rsi + proc.rip]
	mov	r9, [rbp + gseg.process]
	call	printf
%if log_switch_to > 1
	call	print_procstate
%endif
	mov	rax, rbx
%endif

	bt	dword [rax + proc.flags], PROC_ON_RUNQUEUE_BIT
	jnc	.not_blocked
	PANIC
.not_blocked:

	; Update pointer to current process
	mov	rbx, [rbp + gseg.process]
	test	rbx, rbx
	jz	.no_prev_proc
	; Require that previous/current process is already null? We should not
	; surprise-switch until we've e.g. saved all registers...
	and	[rbx + proc.flags], byte ~PROC_RUNNING
.no_prev_proc:
	mov	[rbp + gseg.process], rax
	or	[rax + proc.flags], byte PROC_RUNNING

	; If switching back before anything else uses the FPU, don't set TS
	cmp	rax, [rbp + gseg.fpu_process]
	je	.no_set_ts
	mov	rbx, cr0
	bts	ebx, CR0_TS_BIT
	jc	.no_set_ts
	mov	cr0, rbx
.no_set_ts:

	; Make sure we don't invalidate the TLB if we don't have to.
	mov	rcx, [rax + proc.cr3]
	mov	rbx, cr3
	cmp	rbx, rcx
	je	.no_set_cr3
	mov	cr3, rcx
.no_set_cr3:

.user_exit:
	; If we stop disabling interrupts above, this will be wildly unsafe.
	; For now, we rely on the flags-restoring part below to atomically
	; restore flags and go to user mode. The risk is if we switch "from
	; kernel" while having the user GS loaded!
	swapgs
	bt	dword [rax + proc.flags], PROC_FASTRET_BIT
	jc	fastret

	bt	qword [rax + proc.rflags], RFLAGS_IF_BIT
	jnc	.ret_no_intrs

	; Push stuff for iretq
	push	user_ds
	push	qword [rax + proc.rsp]
	push	qword [rax + proc.rflags]
	push	user_cs
.restore_and_iretq:
	push	qword [rax + proc.rip]
	; rax is first, but we'll take it last...
	lea	rsi, [rax + proc.rax + 8]

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

.ret_no_intrs:
	PANIC

; Return to process in 'rax' using fastret.
; Note: This must be the same process as was last running, otherwise it'll have
; the wrong address space.
; The return value should be stored in [rax + proc.rax]. All other registers are
; overwritten before returning.
; syscall-clobbered registered are cleared unless you use the .no_clear entry-
; point.
; If you do not know that rax is the current process, you must use switch_to
; instead. But that should be cheap for the fastret case.
; This does *not* swapgs, that must be run before getting here.
; TODO Use another register than 'rax' (preferrably one of the ones that get
; clobbered by sysret) - then rax can contain the return value to the process.
fastret:
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

	clear_clobbered_syscall
.no_clear:
	mov	rbx, cr3
	cmp	rbx, [rax + proc.cr3]
	jne	.wrong_cr3
	load_regs rbp,rbx,r12,r13,r14,r15
.fast_fastret:
	mov	rsp, [rax+proc.rsp]
	mov	rcx, [rax+proc.rip]
	mov	r11, [rax+proc.rflags]
	mov	rax, [rax+proc.rax]
	o64 sysret
.wrong_cr3:
	PANIC
.from_recv:
	load_regs rdi,rsi,rdx,r8,r9,r10
	jmp	.no_clear

; End switch

; Allocate a frame and return its *virtual* address in the physical-memory
; mapping area. Subtract kernel_base (eugh, wrong name) to get the actual
; physical address.
allocate_frame:
	call	allocate_frame.nopanic

%if log_alloc
lodstr	rdi, 'allocate_frame rip=%p val=%p', 10
	mov	rsi, [rsp]
	mov	rdx, rax
	push	rax
	call	printf
	pop	rax
%endif

	test	rax,rax
	jz	.panic
	ret
.panic:
	PANIC

.nopanic:
	mov	rax, [rbp + gseg.free_frame]
	test	rax, rax
	; If local stack is out of frames, steal from global stack
	jz	.steal_global_frames

	mov	rsi, [rax]
	mov	[rbp + gseg.free_frame], rsi
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
	mov	[rbp + gseg.free_frame], rcx
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

	mov	rdi, rax
%if unroll_memset_0
	add	rdi, 128
	mov	ecx, 16
	; Clear the task-switched flag while we reuse some registers
	mov	rdx, cr0
	clts
	movdqu	[rbp + gseg.temp_xmm0], xmm0
	xorps	xmm0, xmm0

.loop:
%assign i -128
%rep 16
	movntdq	[rdi + i], xmm0
%assign i i+16
%endrep
	add	rdi, 16*16
	loop	.loop

	movdqu	xmm0, [rbp + gseg.temp_xmm0]
	; Reset TS to whatever it was before
	mov	cr0, rdx
%else
	mov	ecx, 4096/8
	zero	eax
	rep stosq
	lea	rax, [rdi - 4096]
%endif

.ret_oom:
	; TODO release global-page-structures spinlock
	ret

; CPU-local garbage-frame stack? Background process for trickling cleared pages into cpu-local storage?
free_frame:
	test	rdi, rdi
	jz	.ret

.not_null:
%if log_alloc
	push	rdi
	mov	rsi, rdi
lodstr	rdi,	'free_frame %p', 10
	call	printf
	pop	rdi
%endif

	; TODO acquire global-page-structures spinlock
	lea	rax, [rel globals.garbage_frame]
	mov	rcx, [rax]
	mov	[rdi], rcx
	mov	[rax], rdi
	; TODO release global-page-structures spinlock
.ret	ret

struc dlist
	.head	resq 1
	.tail	resq 1
endstruc
struc dlist_node
	.next	resq 1
	.prev	resq 1
endstruc
struc dict
	.root	resq 1
endstruc
struc dict_node
	.key	resq 1
	.left	resq 1
	.right	resq 1
endstruc
; These are stored in some map/search structure, to support lookups from an
; int64 in this aspace to a process (that should receive messages) and the
; corresponding remote int64 (or null if not yet associated there).
; associate: always done by copying an existing handle. We copy the proc pointer
; and increase the proc's refcount, but reset other to null. Then insert into
; the map.
; dissociate: remove from dictionary, set other's other to null, deref proc,
; deallocate
; send: lookup handle by key, fetch proc pointer, look at other to see what key
; should be "received from" when it gets the message
; (if other == null, we need the receiver to be in a receive-and-associate
; mode with a handle to receive - and need to associate before receiving)
; receive:
; * if null, receive-from-any and discard the source ID if not already mapped
;   in the receiver
; * lookup handle by key:
;   * if associated, proceed with a receive-from-specific. We already know the
;     handle to have received from and don't need to fiddle with it.
;   * not associated, finish association to proc and return
;
; associated = proc is not null
struc handle
	.dnode	restruc dict_node
	.key	equ handle.dnode + dict_node.key
	.proc	resq 1
	; pointer to other handle if any. Its 'key' field is the other-name that
	; we need when e.g. sending it a message. If null this is not associated
	; in other-proc yet.
	.other	resq 1
	.events	resq 1
endstruc

struc pending_pulse
	.dnode	restruc dict_node
	.key	equ pending_pulse.dnode + dict_node.key
	.handle	resq 1
endstruc

; Ordinary and boring flags
MAPFLAG_X	equ 1
MAPFLAG_W	equ 2
MAPFLAG_R	equ 4
MAPFLAG_RWX	equ 7 ; All/any of the R/W/X flags

; Anonymous page: allocate frame on first use
MAPFLAG_ANON	equ 8
; Backdoor flag for physical memory mapping. handle is 0, .offset is (paddr -
; vaddr).
MAPFLAG_PHYS	equ 16
; Mix: automatically allocated page that is "locked" (really it only differs in
; that it's allocated at map time and the physical address is returned to the
; user).
MAPFLAG_DMA	equ (MAPFLAG_PHYS | MAPFLAG_ANON)
MAPFLAG_USER_ALLOWED equ MAPFLAG_PHYS | (MAPFLAG_PHYS - 1)

; mapcard: the handle, offset and flags for the range of virtual addresses until
; the next card.
; 5 words:
; - 3 for dict_node w/ vaddr
; - 1 handle
; - 1 offset+flags
;   (since offsets must be page aligned we have 12 left-over bits)
; This structure is completely unrelated to the physical pages backing virtual
; memory - it represents each process' wishful thinking about how their memory
; should look. backings and sharings control physical memory.
struc mapcard
	.as_node restruc dict_node
	.vaddr	equ .as_node
	.handle	resq 1
	; .vaddr + .offset = handle-offset to be sent to backer on fault
	; For a direct physical mapping, paddr = .vaddr + .offset
	; .offset = handle-offset - vaddr
	; .offset = paddr - .vaddr
	.offset	resq 1
	.flags	equ .offset ; low byte (12 bits?) of offset is flags
endstruc

; backing: mapping *one page* to the place that page came from.
; Indexed by vaddr for the process that maps it. The vaddr includes flags, so
; look up by vaddr|0xfff.
; This is likely to exist once per physical page per process. Should be
; minimized.
; 6 words:
; - 3 words for dict_node w/ vaddr
; - 1 word for parent
; - 2 words for child-list links
;
; Could be reduced to 4 words: flags, parent, child-list links, if moving to
; an external dictionary. 32 bytes per page gives 128 entries in the last-level
; table. Flags could indicate how many levels that are required, and e.g. a
; very small process could have only one level, and map 128 pages at 0..512kB.
struc backing
	.as_node restruc dict_node
	.vaddr	equ .as_node
	; Flags stored in low bits of vaddr!
	.flags	equ .vaddr
	; Pointer to parent sharing. Needed to unlink self when unmapping.
	; Could have room for flags (e.g. to let it be a paddr when we don't
	; need the parent - we might have a direct physical address mapping)
	.parent	resq 1
	; Space to participate in parent's list of remappings.
	.child_node restruc dlist_node
endstruc

; sharing: mapping one page to every place it's been shared to
; 7 words!
struc sharing
	.as_node restruc dict_node
	.vaddr	equ .as_node
	.paddr	resq 1
	.aspace	resq 1
	.children restruc dlist
endstruc

struc aspace
	; Upon setup, pml4 is set to a freshly allocated frame that is empty
	; except for the mapping to the kernel memory area (which, as long as
	; it's less than 4TB is only a single entry in the PML4).
	.pml4		resq 1
	; TODO Lock structure for multiprocessing
	.count		resq 1
	; Do we need a list of processes that share an address space?
	; (That would remove the need for .count, I think.)
	;.procs	resq 1
	.handles	restruc dict
	.pending	restruc dict

	.mapcards	restruc dict
	.backings	restruc dict
	.sharings	restruc dict
endstruc

section .text

; rdi = dlist
; rsi = dlist_node
; Non-standard calling convention!  Clobbers r8 but preserves all other
; registers
dlist_append:
	mov	r8, [rdi + dlist.tail]
	test	r8, r8
	mov	[rdi + dlist.tail], rsi
	jz	.empty
	mov	[rsi + dlist_node.prev], r8
	mov	[r8 + dlist_node.next], rsi
	ret
.empty:
	mov	[rdi + dlist.head], rsi
	ret

%if 0
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
	ret
.empty:
	mov	[rdi + dlist.tail], rsi
	ret
%endif

; rdi = dlist
dlist_pop:
	mov	rsi, [rdi + dlist.head]
	test	rsi, rsi
	jz	dlist_remove.not_tail
	; Non-zero: continue to remove the node in rsi

; rsi = dlist_node (must be in the list)
dlist_remove:
	mov	rcx, [rsi + dlist_node.prev]
	mov	rax, [rsi + dlist_node.next]
	zero	edx
	mov	[rsi + dlist_node.prev], rdx
	mov	[rsi + dlist_node.next], rdx
	test	rcx, rcx
	jz	.no_prev
	mov	[rcx + dlist_node.next], rax
.no_prev:
	test	rax, rax
	jz	.no_next
	mov	[rax + dlist_node.prev], rcx
.no_next:

	; If we remove the head/tail, replace it with our next/prev. It doesn't
	; matter if next/prev is null, because if we are the tail and have no
	; prev we were the only element.
	cmp	[rdi + dlist.head], rsi
	jne	.not_head
	mov	[rdi + dlist.head], rax
.not_head:

	cmp	[rdi + dlist.tail], rsi
	jne	.not_tail
	mov	[rdi + dlist.tail], rcx
.not_tail:

	mov	rax, rsi
	ret

; rdi: aspace
; rsi: vaddr
; rdx: offset + flags
; rcx: handle
mapcard_set:
	push	rdx
	push	rcx
	push	rdi
	push	rsi
	call	aspace_find_mapcard
	test	rax, rax
	jz	.add_new
	mov	rsi, [rsp]
	cmp	[rax + mapcard.vaddr], rsi
	jne	.add_new
	pop	rsi
	pop	rdi
	je	.set_and_ret
.add_new:
	call	allocate_frame
	pop	qword [rax + mapcard.vaddr]
	mov	rsi, rax
	pop	rdi
	add	rdi, aspace.mapcards
	call	dict_insert
.set_and_ret:
	pop	qword [rax + mapcard.handle]
	pop	qword [rax + mapcard.offset]
	ret

; rdi: the address space being mapped into
; rsi: the virtual address to map the region at
; rdx: the end of the virtual mapping
; rcx: handle to map
; r8: offset + flags
map_range:
	push	r8
	push	rcx
	push	rdx
	push	rdi
	push	rsi

	; Get mapcard for end address
	; Must get before inserting at vaddr (may be covered by a card that
	; lies before vaddr).
	mov	rsi, rdx
	call	aspace_find_mapcard
	test	rax, rax
	zero	edx
	jz	.no_end_mapcard
	mov	rcx, [rax + mapcard.handle]
	mov	rdx, [rax + mapcard.offset]
.no_end_mapcard:

	cmp	rdx, [rsp + 32]
	jne	.change_end
	cmp	rcx, [rsp + 24]
	je	.no_change_end

	; Insert (overwriting) the start and end cards.
	;if endcard != card {
	;	self.mappings.insert(end, endcard);
	;}
.change_end:
	mov	rdi, [rsp + 8]
	mov	rsi, [rsp + 16] ; end address
	; rdx = offset of old end-of-range card
	; rcx = handle
	call	mapcard_set
	jmp	.set_start
.no_change_end:
	;remove end card, it's equivalent to the start-card we're adding
	mov	rdi, [rsp + 8]
	mov	rsi, [rsp + 16]
	lea	rdi, [rdi + aspace.mapcards]
	call	dict_remove
.set_start:
	;self.mappings.insert(vaddr, card);
	mov	rdi, [rsp + 8]
	mov	rsi, [rsp] ; start address
	mov	rdx, [rsp + 32]
	mov	rcx, [rsp + 24]
	call	mapcard_set

%if log_mappings
lodstr	rdi, 'map_range: removing old range', 10
	call	printf
%endif

	; Find all cards vaddr < key < end and remove them.
.delete:
	mov	rdi, [rsp + 8] ; aspace
	mov	rsi, [rsp] ; start address
	mov	rdx, [rsp + 16] ; end address
	lea	rdi, [rdi + aspace.mapcards]
	call	dict_remove_range_exclusive
	test	rax, rax
	jz	.ret
	push	rax

%if log_mappings
lodstr	rdi, 'map_range: removing %p', 10
	mov	rsi, rax
	call	printf
%endif

	pop	rdi
	call	free_frame
	jmp	.delete

.ret:
%if log_find_mapping
lodstr	rdi, 'map_range done?', 10
	call	printf
	mov	rdi, [rsp + 8] ; aspace
	mov	rsi, [rsp] ; start address
	call	aspace_find_mapcard
	mov	rdi, [rsp + 8]
	mov	rsi, [rsp + 16] ; end address
	call	aspace_find_mapcard
lodstr	rdi, 'map_range done.', 10
	call	printf
%endif
	add	rsp, 40
	ret

; rdi: the address space being searched
; rsi: the local address (key) of a handle
; =>
; rax: pointer to 'handle' struct or 0
find_handle:
	lea	rdi, [rdi + aspace.handles]
%if log_find_handle
	push	rdi
	push	rsi
	mov	rdx, rdi
lodstr	rdi,	'find_handle for %x in %p', 10
	call	printf
	pop	rsi
	pop	rdi
	call	dict_lookup
	push	rax
lodstr	rdi,	'find_handle found %p', 10
	mov	rsi, rax
	call	printf
	cmp	qword [rsp], 0
	jz	.no_handle_found
	; found a handle, does it have an other-end?
	zero	edx
	mov	rax, [rsp]
	mov	rsi, [rax + handle.other]
	test	rsi, rsi
	jz	.no_other
	mov	rsi, [rsi + handle.key]
.no_other:
	mov	rdx, [rax + handle.proc]
lodstr	rdi,	'-> %x in %p', 10
	call	printf
.no_handle_found:
	pop	rax
	ret
%else
	tcall	dict_lookup
%endif
	; assume dict_node comes first in each handle


; rdi: dictionary to insert into
; rsi: entry to insert
; =>
; rax: input rsi
dict_insert:
	;mov	rdx, [rsi + dict_node.key]
	; Fishy dictionary!
	; just set node->left = 0, node->right = root, root = node
	zero	eax
	mov	[rsi + dict_node.left], rax
	mov	rax, rsi
	xchg	rsi, [rdi + dict.root]
	mov	[rax + dict_node.right], rsi
	ret

; rdi: dictionary to find in
; rsi: key to find
; =>
; rax: entry or null
dict_lookup:
	mov	rax, [rdi + dict.root]
.loop:
	test	rax, rax
	jz	.done
	cmp	[rax + dict_node.key], rsi
	jz	.done
	mov	rax, [rax + dict_node.right]
	jmp	.loop
.done:
	ret

; rdi: dictionary to find in
; rsi: key to find
; =>
; rax: entry with largest key <= rsi, or null if no entries found
dict_find_lessthan:
	mov	rdi, [rdi + dict.root]
	zero	eax
	zero	ecx
.loop:
	test	rdi, rdi
	jz	.done
	cmp	[rdi + dict_node.key], rcx
	jb	.no_match
	cmp	[rdi + dict_node.key], rsi
	ja	.no_match
	mov	rax, rdi
	mov	rcx, [rdi + dict_node.key]
.no_match:
	mov	rdi, [rdi + dict_node.right]
	jmp	.loop
.done:
	ret

dict_remove:
	lea	rdx, [rdi + dict.root - dict_node.right]
.loop:
	mov	rax, [rdx + dict_node.right]
	test	rax, rax
	jz	.done_end
	cmp	[rax + dict_node.key], rsi
	jz	.done_found
	mov	rdx, rax
	jmp	.loop
.done_found:
	mov	rcx, [rax + dict_node.right]
	mov	[rdx + dict_node.right], rcx
.done_end:
	; rax == removed node (if any)
	ret

; rdi: dict
; rsi: lower key
; rdx: upper key
; Remove all items with keys between rsi and rdx exclusive.
dict_remove_range_exclusive:
	lea	rcx, [rdi + dict.root - dict_node.right]
.loop:
	mov	rax, [rcx + dict_node.right]
	test	rax, rax
	jz	.done_end
	cmp	[rax + dict_node.key], rsi
	jbe	.dont_delete
	cmp	[rax + dict_node.key], rdx
	jb	.done_found
.dont_delete:
	mov	rcx, rax
	jmp	.loop
.done_found:
	mov	rdx, [rax + dict_node.right]
	mov	[rcx + dict_node.right], rdx
.done_end:
	; rax == removed node (if any)
	ret

; Add a preallocated and filled-in handle object to a process' dictionary of
; handles.
; rdi: the process to modify
; rsi: the handle to insert
proc_insert_handle:
	mov	rdi, [rdi + proc.aspace]
	lea	rdi, [rdi + aspace.handles]
	tcall	dict_insert

; rdi: the address space being removed from
; rsi: the local address (key) of the handle being removed
delete_handle:
	lea	rdi, [rdi + aspace.handles]
	call	dict_remove
	test	rax, rax
	mov	rdi, rax
	tcall	free_frame

; rdi: the address space being mapped into
; rsi: the local address (key) being mapped
; rdx: the other-process being mapped
; =>
; rax: the handle object we just mapped
;
; This will replace a previous handle object if any, making it point to the
; new other-process and severing the old link to another process.
map_handle:
	push	rdi
	push	rsi
	push	rdx
	call	find_handle
	; rax = previous handle at 'rsi', if any
	test	rax, rax
	jnz	.found_existing

%if log_new_handle
lodstr	rdi,	'%p mapping new handle: %x (proc=%p)', 10
	mov	rsi, [rsp + 16]
	mov	rdx, [rsp + 8]
	mov	rcx, [rsp]
	call	printf
%endif

	; Allocate and insert a new handle

	call	allocate_frame
	; rax = newly allocated handle object
	pop	rdx
	pop	qword [rax + handle.key]
	mov	[rax + handle.proc], rdx
	inc	qword [rdx + proc.count]

	pop	rdi
	mov	rsi, rax
	lea	rdi, [rdi + aspace.handles]
	; rdi = address space's handle dictionary
	; rsi = handle to insert
	tcall	dict_insert

.found_existing:
	pop	rdi ; other-process, keep this
	; Then also pop the stuff we don't need anymore
	pop	rsi
	pop	rsi
	; Is there a remote association for this handle yet?

	; A handle with no remote proc doesn't exist: those are in effect the
	; same as handles that aren't in the map, so we just shouldn't have
	; those in the map in the first place :)
	;cmp	qword [rax + handle.proc], 0

	; If other != null we need to dissociate this handle before we proceed.
	mov	rsi, [rax + handle.other]
	test	rsi, rsi
	jz	.no_other
	; There's an other-handle here. Sever the link by clearing the other-
	; pointers. Their handle remains pointing to this process, our handle
	; will have its process updated in the next step.
	zero	edx
	mov	[rsi + handle.other], rdx
	mov	[rax + handle.other], rdx

.no_other:
	; rax = current handle, rax->other = null, rax->proc = some process
	; rdi = process to reference

	; Reference new process, dereference old process
	; Assume proc != null for all handles
	inc	qword [rdi + proc.count]
	xchg	rdi, [rax + handle.proc]
	;TODO call	deref_proc or something
	dec	qword [rdi + proc.count]

	; rax = handle
	ret

; rdi: address space of handle
; rsi: handle object to pulse
; rdx: pulses to add
pulse_handle:
	mov	rax, [rsi + handle.events]
.retry	mov	rcx, rdx
	or	rcx, rax
	; if events == rax (old events), replace with rdx
	cmpxchg	qword [rsi + handle.events], rcx
	jne	.retry
	; rax now contains the previous value of events.

	test	rax, rax
	jnz	.already_pending

	; The events mask was 0 before we got here - we need to add it to the
	; pending map.
	push	rdi
	push	rsi
	call	allocate_frame
	pop	rsi
	mov	[rax + pending_pulse.handle], rsi
	mov	rsi, [rsi + handle.key]
	mov	[rax + pending_pulse.key], rsi
	pop	rdi
	lea	rdi, [rdi + aspace.pending]
	mov	rsi, rax
	call	dict_insert

.already_pending:
.ret	ret

; rdi: address space
; returns the first pending handle with a non-zero pulses field
; rax: pulses pending
; rdx: handle
get_pending_handle:
	push	rdi

.handle:
	mov	rsi, [rdi + aspace.pending + dict.root]
	test	rsi, rsi
	jz	.ret_null

	mov	rsi, [rsi + pending_pulse.handle]
	push	rsi
	call	get_pending_pulse
	pop	rdx
	; rax = pending pulses
	test	rax, rax
	jnz	.ret

	mov	rdi, [rsp]
	jmp	.handle

.ret_null:
	zero	eax
	zero	edx
.ret:	pop	rdi
	ret

; rdi: address space
; rsi: handle object
; returns the set of events that were pending on that handle object, if any.
; Also removes the handle from the pending list.
get_pending_pulse:
	; old events -> rax, 0 -> events
	mov	rax, [rsi + handle.events]
.retry	zero	ecx
	cmpxchg	qword [rsi + handle.events], rcx
	jne	.retry

	; rax = set of pending events, push so we can return it later.
	push	rax
	; The set of events was not empty, the handle was queued and should now
	; be unqueued.
	lea	rdi, [rdi + aspace.pending]
	mov	rsi, [rsi + handle.key]
	call	dict_remove

	mov	rdi, rax
	call	free_frame

	pop	rax
.ret	ret

; rdi: address space to add mapping to
; rsi: physical address of page to add, plus relevant flags as they will appear
; in the page table entry.
; rdx: vaddr to map it to. May be unaligned for convenience.
add_pte:
	push	r12
	push	rsi
	push	rdx
	push	rdi

%if log_mappings
	lea	rdi, [mapping_page_to_frame]
	call	printf
%endif

	pop	rdi
	mov	rdx, [rsp]
	mov	rsi, [rsp + 8]

%macro index_table 4 ; source, shift, base, target for address
	mov	rcx, %1
	shr	rcx, %2 - 3
	and	ecx, 0xff8 ; Then 'and' away the boring bits
	lea	%4, [%3 + rcx]
%endmacro

%macro do_table 2 ; shift and name
	index_table [rsp], %1, rdi, r12

%if log_mappings
lodstr	rdi,	'Found ', %2, ' %p at %p', 10
	mov	rsi, [r12]
	mov	rdx, r12
	call	printf
%endif

	test	byte [r12], 1
	jnz	%%have_entry

	cmp	qword [r12], 0
	;PANIC: Not present in page table, but it has some data. This shouldn't happen :)
	jnz	.panic

	; No PDP - need to allocate new table
	call	allocate_frame
	lea	rdi, [rax - kernel_base]
	mov	[r12], rdi

%if log_mappings
lodstr	rdi,	'Allocated ', %2, ' at %p', 10
	mov	rsi, rax
	call	printf
%endif

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
	test	byte [r12], 1
	; We should probably check and handle mapping the same page twice as a
	; no-op. Or maybe not - since it indicates other code tried to map
	; something - getting a fault for an already-mapped page might mean the
	; user code is getting an actual fault of some kind and should be
	; killed insted.
	jnz	.panic

	pop	rdx
	pop	rsi
	mov	[r12], rsi
%if (log_mappings || log_add_pte)
lodstr	rdi, 'Mapping %p to %p (at %p)!', 10
mapping_page_to_frame equ _STR
	mov	rcx, r12
	call	printf
%endif
	pop	r12

	ret

.panic:
	PANIC

; rax is already saved, and now points to the process base
; rbp is already saved (points to the gseg, but not used by this function)
; rsp, rip, rflags are on the stack in "iretq format"
; returns with ret; call with call
save_from_iret:
	; Save the rsp so we can fiddle with it here and restore before ret.
	mov	[rax+proc.rcx], rcx
	mov	rcx, rsp

	add	rsp,8 ; return address from 'call save_from_iret'
	pop	qword [rax+proc.rip]
	add	rsp,8 ; CS
	pop	qword [rax+proc.rflags]
	pop	qword [rax+proc.rsp]
	add	rsp,8 ; SS

	; Reset fastret flag so that iretq is used next time
	; FIXME Fastret should never be set except between a safe switch-out and
	; the next context switch (i.e. fastret should reset the flag, not us)
	and	[rax+proc.flags], byte ~(PROC_FASTRET | PROC_RUNNING)

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
	push	rdi
	push	rsi
	; rsp and rbp are already saved
	; ... should rsp be stored outside the gpr file though?
	sub	rsp,16
	push	rbx
	push	rdx
	; rcx is already saved
	; rax is already saved

	mov	rsp, rcx
	ret

handler_NM: ; Device-not-present, fpu/media being used after a task switch
	push	rbp
	push	rax
	; FIXME If we get here in kernel mode?
	swapgs

	clts

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
%if log_fpu_switch
	; FIXME printf may clobber more stuff than what we've actually saved.
	; All caller-save registers must be preserved since we're in an
	; interrupt handler.

lodstr	rdi,	'FPU-switch: %p to %p', 10
	mov	rsi,[rbp+gseg.fpu_process]
	mov	rdx,[rbp+gseg.process]
	call	printf
%endif

	; Find the previous process and save fpu/media state to its process struct
	mov	rax,[rbp+gseg.fpu_process]
	test	rax,rax
	; No previous process has unsaved fpu state, just load this process' state
	jz	.no_save_state
	; We are already the FPU process, but we might have been switched away
	; from.
	cmp	rax, [rbp + gseg.process]
	je	.no_restore_state

	; Save FPU state in process rax
	o64 fxsave [rax+proc.fxsave]
.no_save_state:
	; Restore FPU stste for current process
	mov	rax, [rbp+gseg.process]
	o64 fxrstor [rax+proc.fxsave]
	; FPU state now owned by current process
	mov	[rbp+gseg.fpu_process], rax

.no_restore_state:
	pop	rax
	pop	rbp
	swapgs
	iretq

; rdi = aspace
; rsi = virtual address
aspace_find_mapcard:
	add	rdi, aspace.mapcards
%if log_find_mapping == 0
	tcall	dict_find_lessthan
%else
	push	rsi
	call	dict_find_lessthan
	push	rax
lodstr	rdi,	'find_mapcard: %p -> %x.. +%x (%x)', 10
	pop	rax ; mapcard
	pop	rsi
	zero	edx
	zero	ecx
	zero	r8
	test	rax, rax
	jz	.nothing
	mov	rdx, [rax + mapcard.vaddr]
	mov	rcx, [rax + mapcard.offset]
	mov	r8, rsi
	and	r8w, ~0xfff
	add	r8, rcx
.nothing:
	push	rax
	call	printf
	pop	rax
	ret
%endif

aspace_find_backing:
	push	rsi
	or	si, 0xfff
	add	rdi, aspace.backings
	call	dict_find_lessthan
%if log_find_backing
	mov	rsi, [rsp]
	push	rax
	zero	edx
	zero	ecx
	test	rax, rax
	jz	.no_backing
	mov	rdx, [rax + backing.vaddr]
	mov	rcx, [rax + backing.parent]
	test	dl, MAPFLAG_PHYS
	jnz	.parent_was_paddr
	mov	rcx, [rcx + sharing.paddr]
.parent_was_paddr:
.no_backing:
lodstr	rdi, 'find_backing: %p -> v %x p %x', 10
	call	printf
	pop	rax
%endif
	test	rax, rax
	pop	rsi
	jz	.ret
	and	si, ~0xfff
	cmp	[rax + backing.vaddr], rsi
	; if backing.vaddr < rsi (vpage), then we found the wrong page.
	jae	.ret
	zero	eax
.ret:
	ret

; rdi = aspace
; rsi = backing
aspace_insert_backing:
	lea	rdi, [rdi + aspace.backings]
	tcall	dict_insert

; rdi = aspace
; rsi = vaddr
; => rax = sharing, potentially a new one without paddr filled in.
aspace_add_sharing:
	push	rdi
	lea	rdi, [rdi + aspace.sharings]
	push	rsi
	call	dict_lookup
	test	rax, rax
	jz	.no_sharing
.ret:
	pop	rsi
	pop	rdi
	ret

.no_sharing:
	call	allocate_frame
	mov	rsi, rax
	pop	qword [rax + sharing.vaddr]
	pop	rdi
	mov	[rax + sharing.aspace], rdi
	lea	rdi, [rdi + aspace.sharings]
	tcall	dict_insert

; rdi = aspace
; rsi = vaddr
; rdx = sharing to add to
aspace_add_shared_backing:
	push	rdi
	push	rsi
	push	rdx
	call	allocate_frame
	pop	rdi
	mov	[rax + backing.parent], rdi
	pop	qword [rax + backing.vaddr]
	lea	rdi, [rdi + sharing.children]
	mov	rsi, rax
	call	dlist_append
	; dlist_append leaves the backing in rsi
	pop	rdi
	lea	rdi, [rdi + aspace.backings]
	tcall	dict_insert

not_hosed_yet:
	PANIC
kernel_fault:
	PANIC
hosed:
	cli
	hlt
	mov	al,0xe

handler_PF:
	; TODO Put the kernel stack in virtual memory so we can add a sentinel
	; page.
	cmp	rsp, phys_vaddr(pages.kernel_stack + 0xf00)
	jle	not_hosed_yet
	cmp	rsp, phys_vaddr(pages.kernel_stack + 0x200)
	jle	hosed
	test	byte [rsp], 0x4
	jz	kernel_fault

	push	rax
	push	rbp
	swapgs

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
	mov	rax, [rbp + gseg.process]
	; The rax and rbp we saved above, store them in process
	pop	qword [rax + proc.rbp]
	pop	qword [rax + proc.rax]
	; Copy page-fault code further up the stack
	pop	qword [rsp - 24]
	call	save_from_iret
	push	qword [rsp - 24]

	; Page-fault error code:
	; Bit 0: "If this bit is cleared to 0, the page faultwas caused by a not-present page. If this bit is set to 1, the page fault was caused by a page-protection violation.
	; Bit 1: clear = read access, set = write access
	; Bit 2: set = user-space access, cleared = supervisor (tested above)
	; Bit 3: RSV - a reserved bit in a page table was set to 1
	; Bit 4: set = the access was an instruction fetch. Only used when NX is enabled.
	; Bit 9: ??
	; Bit 16: ??

%if log_page_fault
lodstr	rdi,	'Page-fault: cr2=%x error=%x proc=%p rip=%x', 10
	mov	rsi, cr2
	; Fault
	mov	rdx, [rsp]
	mov	rcx, rax
	mov	r8, [rax + proc.rip]
	call printf
%endif

	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rsi, cr2
	mov	[rdi + proc.fault_addr], rsi
	call	aspace_find_backing
	test	rax, rax
	jnz	.found_backing

.no_backing:
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rsi, cr2
	call	aspace_find_mapcard
	test	rax, rax
	jnz	.found_mapcard

lodstr	r12,	'No mapping found!', 10
.invalid_match:
%if log_page_fault
	mov	rdi, r12
	call	printf
%endif
	PANIC

.found_backing:
	; rax+backing.vaddr has flags - check if we have enough access for the
	; fault. If not jump to .no_backing.
	mov	r12, rax
%if log_mappings
lodstr	rdi,	'Backing found:', 10, 'cr2=%p map=%p vaddr=%p', 10
	mov	rsi, cr2
	mov	rdx, rax
	mov	rcx, [rax + backing.vaddr]
	call	printf
%endif

	; rsi is fault address (though we already know which mapping we're
	; dealing with)
	mov	rsi, cr2
	mov	rdi, r12

	mov	eax, [rdi + backing.vaddr]
	mov	esi, eax
	test	eax, MAPFLAG_RWX
	; We had no permissions at all, must fault the page in
	jz	.no_backing
	; Was the access a write access? (bit 1: set == write)
	test	byte [rsp], 0x2
	jz	.not_a_write
	; Write to a read-only page: fault
	test	eax, MAPFLAG_W
	jz	.no_backing
.not_a_write:
	; TODO Check for access-during-instruction-fetch for a present NX page

.have_backing:
	; rdi: backing that should be mapped
	; look up the parent to get the paddr
	; combine with flags from backing
	mov	rsi, [rdi + backing.parent]
	test	byte [rdi + backing.vaddr], MAPFLAG_PHYS
	jnz	.parent_was_paddr
	mov	rsi, [rsi + sharing.paddr]
.parent_was_paddr:

	mov	eax, [rdi + backing.flags]
	; Some notes on how to (eventually) set PTE flags according to region
	; flags:
	; - In general, region permissions from region.flags are what should be
	;   set in the PTE.
	; - CoW: the region is set read-only with the CoW flag set, until we
	;   have copied the data onto new pages - then we can update the page
	;   frame pointer to the new private pages and change flags to R/W,
	;   then without the CoW flag (nothing more to copy-on-write).
	; - Anonymous: start out simply as a mapping of a null region, then we
	;   create a region that CoW-maps a shared null page, then let CoW
	;   take care of replacing it with a private null page and update the
	;   permissions.
	or	rsi, 5 ; Present, User-accessible
	test	eax, MAPFLAG_X
	jnz	.has_exec
	; Set bit 63 to *disable* execute permission
	bts	rsi, 63
.has_exec:
	test	eax, MAPFLAG_W
	jz	.no_write_access
	or	rsi, 2
.no_write_access:
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rdx, cr2 ; Note: lower 12 bits will be ignored automatically
	call	add_pte

	; TODO Handle failures by killing the process.

.ret:
	mov	rax, [rbp + gseg.process]
	jmp	switch_to

; mapcard in rax
.found_mapcard:
	mov	rdi, rax
	mov	eax, [rax + mapcard.flags]
%if log_page_fault
lodstr	r12,	'Mapcard has no access', 10
%endif
	test	eax, MAPFLAG_RWX
	jz	.invalid_match

	cmp	qword [rdi + mapcard.handle], 0
	jz	.no_handle

; The mapping is a mapped handle that lacks a backing region. Set up a PFAULT
; IPC to the handle it came from, make this process wait for a response, switch
; to something else for a while.
; rdi = mapping
.user_pfault:
	mov	rax, [rbp + gseg.process]
	mov	[rax + proc.fault_addr], rdi ; FIXME Should be the vaddr, not the mapcard pointer
	or	[rax + proc.flags], byte PROC_PFAULT
	PANIC

.no_handle:
%if log_page_fault
lodstr	r12,	'null handle.', 10
%endif
	test	eax, MAPFLAG_ANON | MAPFLAG_PHYS
	jz	.invalid_match

	mov	r12, rdi

	mov	rdi, cr2
	and	di, ~0xfff
	; rdi = vaddr (vpage), rsi = paddr = vaddr + offset
	mov	rsi, [r12 + mapcard.offset]
	add	rsi, rdi
	; Add flags to vaddr
	and	ax, 0xfff
	or	di, ax

	mov	rdx, [rbp + gseg.process]
	mov	rdx, [rdx + proc.aspace]

	test	al, MAPFLAG_PHYS
	jnz	.map_phys

	mov	rsi, rdx
	call	new_anon_backing

	mov	rdi, rax
	jmp	.have_backing

.map_phys:
	; eax is flags from the mapccard we started from
	; rsi is the physical address
	; rsi now vaddr + flags
	; rdx is aspace
	call	new_phys_backing

	mov	rdi, rax
	jmp	.have_backing

.no_mappings:
	jmp	not_hosed_yet

.message_kp:
	;db 'Oh noes, kernel panic! fault addr %p error %p', 10, 10, 0

; rdi: flags and vaddr for new anon page
; rsi: aspace
; => rax is a backing backed by a new anonymous page
new_anon_backing:
	push	rsi
	push	rdi
	; Anonymous mapping: allocate a fresh zero page, create a backing and
	; sharing for it, link it up and then go to the have_backing path.
	call	allocate_frame
	pop	rdi
	lea	rsi, [rax - kernel_base]
	pop	rdx
	tcall	new_phys_backing

; rdi: vaddr with flags
; rsi: phys address to back
; rdx: aspace
; => rax is a new backing
new_phys_backing:
	push	rdx
	push	rsi
	or	di, MAPFLAG_PHYS
	push	rdi
	call	allocate_frame
	pop	qword [rax + backing.vaddr]
	pop	qword [rax + backing.parent]
	pop	rdi
	mov	rsi, rax
	; rdi = aspace, rsi = backing
	call	aspace_insert_backing
	ret

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

	swapgs
	xchg	rsp, [gs:gseg.rsp]
	push	rcx
	; Save all callee-save registers into process, including the things
	; we'd like to clobber, e.g. rbp = gseg
	push	rax
	push	rbp

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
	mov	rax, [rbp + gseg.process]
	pop	qword [rax + proc.rbp]

%macro save_regs 1-*
%rep %0
	mov qword [rax+proc.%1], %1
	%rotate 1
%endrep
%endmacro
	save_regs rbx,r12,r13,r14,r15
	mov	rbx, [rbp + gseg.rsp]
	mov	[rax + proc.rsp], rbx
	lea	rbx, [rsp + 16]
	mov	[rbp + gseg.rsp], rbx
	mov	[rax + proc.rflags], r11
	pop	rbx
	mov	[rax + proc.rax], rbx
	pop	qword [rax + proc.rip]

	cmp	bl, MSG_USER
	jae	syscall_ipc
	cmp	rbx, N_SYSCALLS
	jae	.invalid_syscall
	mov	eax, dword [text_vpaddr(.table) + 4 * rbx]
	add	rax, text_vpaddr(syscall_entry)
	call	rax

.ret_from_generic:
	mov	r11, [rbp + gseg.process]
	mov	[r11 + proc.rax], rax
	mov	rax, r11
	swapgs
	jmp	fastret

.invalid_syscall:
lodstr	rdi, 'Invalid syscall %x! proc=%p', 10
	mov	rsi, rbx
	mov	rdx, [rbp + gseg.process]
	call	printf
	jmp	.ret_from_generic

%macro sc 1
	dd	syscall_ %+ %1 - syscall_entry
%endmacro
.table:
	sc recv
	sc map
	sc pfault
	sc nosys ; UNMAP
	sc hmod
	sc newproc
	; ("temporary") backdoor syscalls
	sc write
	sc portio
	sc grant
	sc pulse
.end_table:
N_SYSCALLS	equ (.end_table - .table) / 4

syscall_nosys:
	pop rdi
	jmp syscall_entry.invalid_syscall

syscall_write:
	; user write: 0x0f00 | char (white on black)
	movzx	edi, dil
	or	di, 0x0f00
	jmp	kputchar.user

kputchar:
	; kernel write: 0x1f00 | char (white on blue)
	movzx	edi, dil
	or	di, 0x1f00
%if bochs_con
	mov	edx, 0xe9
	mov	ecx, 5
lodstr	rsi, 27, '[44m'
	rep outsb
%endif
.user:
	mov	eax, edi
	mov	rdi, [rbp + gseg.vga_pos]
	; escape sequences:
	; blue background: ESC[44m
	; reset: ESC[0m
	; (... if we want to bother with colored output in Bochs)
	cmp	al,10
	je	.newline

%if bochs_con
	out	0xe9, al
	mov	edx, 0xe9
	mov	ecx, 4
lodstr	rsi, 27, '[0m'
	rep outsb
%endif

	stosw

.finish_write:
	cmp	rdi, [rbp + gseg.vga_end]
	jge	.scroll_line
	mov	[rbp + gseg.vga_pos], rdi
.ret:
	clear_clobbered
	ret

.scroll_line:
	mov	rsi, [rbp + gseg.vga_base]
	mov	rdi, rsi
	add	rsi, 160
	mov	ecx, 80*24*2/8
	rep	movsq
	mov	[rbp + gseg.vga_pos], rdi
	zero	eax
	mov	ecx, 160 / 8
	rep	movsq
	jmp	.ret

.newline:
%if bochs_con
	mov	edx, 0xe9
	mov	ecx, 5
lodstr	rsi, 27, '[0m', 10
	rep outsb
%endif

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
	mov	al,0
	rep	stosw

	jmp	.finish_write

syscall_portio:
	; di = port
	; si = flags (i/o) and data size
	; (edx = data)

	; Put port in dx and data in ax, since that's what in/out uses
	xchg	edi, edx
	mov	eax, edi

	cmp	esi, 0x1
	je	.inb
	cmp	esi, 0x11
	je	.outb
	cmp	esi, 0x2
	je	.inw
	cmp	esi, 0x12
	je	.outw
	cmp	esi, 0x4
	je	.indw
	cmp	esi, 0x14
	je	.outdw
	PANIC

.inb:
	in	al, dx
	ret

.inw:
	in	ax, dx
	ret

.indw:
	in	eax, dx
	ret

.outb:
	out	dx, al
	ret

.outw:
	out	dx, ax
	ret

.outdw:
	out	dx, eax
	ret

syscall_gettime:
	movzx	eax, byte [rbp + gseg.curtime]
	ret

syscall_yield:
	mov	rdi, [rbp + gseg.process]
	; Set the fast return flag to return quickly after the yield
	or	[rdi + proc.flags], byte PROC_FASTRET
	and	[rdi + proc.flags], byte ~PROC_RUNNING
	call	runqueue_append
	jmp	switch_next

syscall_hmod:
	; inputs:
	; rdi = source handle
	; rsi = renamed handle
	; rdx = duplicated handle
	;
	; The end result is that rsi's handle (if not null) is an exact
	; duplicate of rdi (linked to the same remote handle), rdi is destroyed
	; (if not same as rsi), and rdx (if not null) is a fresh handle
	; pointing to the same process that rdi pointed to.

%if log_hmod
	push	rdi
	push	rsi
	push	rdx

	mov	r8, rdx
	mov	rcx, rsi
	mov	rdx, rdi
	mov	rsi, [rbp + gseg.process]
lodstr	rdi,	'%p: HMOD %x -> %x, %x', 10
	call	printf

	pop	rdx
	pop	rsi
	pop	rdi
%endif

	; rdi must be non-null (no-op (error) otherwise).
	test	rdi, rdi
	jz	.ret

	push	rsi

	mov	rsi, rdi
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	push	rdi
	push	rdx
	call	find_handle

	test	rax, rax
	jnz	.found_handle
	pop	rax
	pop	rax
	pop	rax
	ret

.found_handle:
	pop	rdx
	push	rax

	; if dup-handle (rdx): create handle pointing to proc
	test	rdx, rdx
	jz	.no_dup

	mov	rsi, rdx ; local-addr of handle
	mov	rdi, [rsp + 8]
	mov	rdx, [rax + handle.proc]
	push	rsi
	call	map_handle
	; dup-handle should not get a pointer to something (but proc should
	; get set?)

%if log_hmod
lodstr	rdi, 'dup_handle: %p (key=%x proc=%p)', 10
	mov	rsi, rax
	mov	rdx, [rax + handle.key]
	mov	rcx, [rax + handle.proc]
	call	printf
%endif

	pop	rsi
	mov	rdi, [rsp + 8]
	call	find_handle

.no_dup:
	; stack: origin-handle aspace rename-handle
	pop	rsi ; origin-handle
	pop	rdi ; aspace
	push	qword [rsi + handle.other]
	push	qword [rsi + handle.proc]
	push	rdi
	mov	rsi, [rsi + handle.key]
	call	delete_handle

	; stack: aspace proc other rename-handle
	pop	rdi ; aspace
	pop	rdx ; proc
	pop	rbx ; other-handle
	pop	rsi ; rename-handle
	zero	eax
	test	rsi, rsi
	jz	.just_delete

	call	map_handle
	mov	[rax + handle.other], rbx
.just_delete:
	test	rbx, rbx
	jz	.no_other
	mov	[rbx + handle.other], rax
.no_other:
	ret

.just_dup:
	; Save process out of handle rdi

.ret:
	ret

syscall_newproc:
;lodstr	rdi, 'hi, newproc here', 10
;	call	printf

	; For supporting loaders, we'd like something more like where we give
	; an image (as a file object or other mappable thing) to the generic
	; loader thingy, which will figure everything else out. Basically
	; create the process with the entry point being in the loader instead.

	; We also want some control over which address space to use for the new
	; process. For now we always create a new unrelated address space.
	; We'll want at least:
	; - fork, make everything the same but CoW
	; - clone/thread, share address space instead of creating a new

	; - Parameters:
	;   rdi = handle of child process
	;   rsi = entry-point/start-of-module
	;   rdx = end-of-module
	;   both as virtual addresses in the parent's address space.
	; - Create new process object
	;   rsp = 1MB (grows down starting at the beginning of the module)
	;   rip = 1MB + (entry-point & 0xfff)
	; - Find the region in the source process that maps the given range,
	;   map that region (read-execute) in the new process at 1MB.
	; - Create a new region for the stack
	; - Go?

	mov	r12, rsi ; entry-point/start
	mov	r13, rdx ; end
	mov	r14, rdi ; handle of child process

	; Find mapping for start..end, make sure they're all the same and a
	; direct physical memory mapping. (That's the only one we support for
	; making processes out of. So far.)
	;
	; (Note that we check the mappings here, not what those pages are
	; backed by in the parent. No shared memory yet...)
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rsi, r12
	call	aspace_find_mapcard
	test	rax, rax
	jz	.panic
	test	byte [rax + mapcard.flags], MAPFLAG_PHYS
	jz	.panic
	push	qword [rax + mapcard.offset]
	mov	byte [rsp], 0

	; TODO Check that everything from start..end is the same!

	; Translate entry-point/end by that physical memory mapping.
	mov	rdi, r12
	add	rdi, [rsp]
	mov	rsi, r13
	add	rsi, [rsp]
	pop	rax

	call	new_proc_simple
	mov	rbx, rax

	; Map handle to child in parent process
	mov	rdi, [rbp + gseg.process]
	mov	rsi, r14 ; local-addr of handle
	mov	rdi, [rdi + proc.aspace]
	mov	rdx, rbx ; child process
	call	map_handle
	push	rax

lodstr	rdi, 'newproc %p at %x', 10
	mov	rsi, rbx
	mov	rdx, r14
	call	printf

	; Map handle to parent in child process
	mov	rdi, [rbx + proc.aspace]
	mov	rsi, r14 ; local-addr of handle
	mov	rdx, [rbp + gseg.process]
	call	map_handle
	mov	[rbx + proc.rdi], r14
	pop	rdi
	mov	[rdi + handle.other], rax
	mov	[rax + handle.other], rdi

	; Child is set up, append it to the run queue
	mov	rdi,rbx
	call	runqueue_append

	ret
.panic:
	PANIC

syscall_halt:
	lodstr	rdi, '<<HALT>>'
	call	printf
	hlt

syscall_send:
	mov	rax, [rbp + gseg.process]
	save_regs rdi,rsi,rdx,r8,r9,r10

.from_other:
	mov	rsi, rdi
	mov	rdi, [rax + proc.aspace]
	; rdi = address space
	; rsi = target process in user address space
	call	find_handle
	; rax = source handle object or null
	test	rax, rax
	jz	.no_target

%if log_messages
	push	rax
lodstr	rdi,	'%p send via %x to %p', 10
	mov	rsi, [rbp + gseg.process]
	mov	rdx, [rsi + proc.rdi]
	mov	rcx, [rax + handle.proc]
	call	printf
	pop	rax
%endif

	mov	rsi, [rbp + gseg.process]
	or	[rsi + proc.flags], byte PROC_IN_SEND
	mov	[rsi + proc.rdi], rax
	mov	rdi, [rax + handle.proc]
	; rdi: target process
	; rsi: source process
	call	send_or_block
	mov	rax, [rbp + gseg.process]
	swapgs
	jmp	fastret

.no_target:
%if log_messages
lodstr	rdi,	'%p send to %x: no handle found', 10
	mov	rsi, [rbp + gseg.process]
	mov	rdx, [rsi + proc.rdi]
	call	printf
%endif
	PANIC

; bx = saved message code, old ax
syscall_ipc:
	mov	rax, [rbp + gseg.process]
	save_regs rdi, rdx, r8, r9, r10

	cmp	bh, MSG_KIND_SEND >> 8
	je	syscall_send

	cmp	bh, MSG_KIND_CALL >> 8
	je	syscall_call

	;cmp	bh, MSG_KIND_REPLYWAIT >> 8
	;je	syscall_replywait

	tcall	syscall_nosys

syscall_recv:
	mov	rax, [rbp + gseg.process]
	save_regs rdi

	mov	rsi, rdi
	test	rsi, rsi
	jnz	.have_handle

%if log_messages
lodstr	rdi, '%p: recv from any', 10
	mov	rsi, rax
	call	printf
%endif
	zero	eax
	jmp	.do_recv

.have_handle:
	mov	rdi, [rax + proc.aspace]
	; rsi = local handle name
	call	find_handle
	; saved rdi: target's handle name for specific source
	; rax: handle object (*if* it already exists!)
	test	rax, rax
	jz	.from_fresh

%if log_messages
	push	rax
lodstr	rdi, '%p: recv from %x in %p', 10
	mov	rsi, [rbp + gseg.process]
	mov	rdx, [rax + handle.other]
	mov	rcx, [rax + handle.proc]
	mov	rdx, [rdx + handle.key]
	call	printf
	pop	rax
%endif

	; non-fresh specific source, we know what we want
	mov	rsi, [rax + handle.proc]
.do_recv:
	mov	rdi, [rbp + gseg.process]
	; rdi = receiving process
	; rax = handle to receive from
	; rsi = sender process, if any
	mov	[rdi + proc.rdi], rax
	or	[rdi + proc.flags], byte PROC_IN_RECV
	call	recv
%if log_messages
lodstr	rdi,	'sync. recv in %p finished (recvd from %x)', 10
	mov	rsi, [rbp + gseg.process]
	mov	rdx, [rsi + proc.rdi]
	call	printf
%endif
	; The receive finished synchronously, continue running this process.
	mov	rax, [rbp + gseg.process]
	swapgs
	jmp	fastret.from_recv

	; saved rdi = handle name
	; no previous handle for rdi
.from_fresh:
%if log_messages
lodstr	rdi, '%p: recv from fresh %x', 10
	mov	rsi, [rbp + gseg.process]
	mov	rdx, [rsi + proc.rdi]
	call	printf
%endif

	mov	rax, [rbp + gseg.process]
	cmp	rax, [rbp + gseg.irq_process]
	jne	.not_irq
%if log_irq
lodstr	rdi, 'recv in irq process, delayed=%x', 10
	mov	rsi, [rbp + gseg.irq_delayed]
	call	printf
%endif
	cmp	qword [rbp + gseg.irq_delayed], 0
	jne	.recv_irq
.not_irq:

	call	allocate_frame
	mov	rdi, [rbp + gseg.process]
	mov	rdx, [rdi + proc.rdi]
	mov	[rax + handle.key], rdx
	zero	esi
	; rax = handle, rdi = receiving process
	jmp	.do_recv

.recv_irq:
	; gseg is CPU-private, no need to synchronize or make atomic.
	zero	esi
	xchg	rsi, [rbp + gseg.irq_delayed]
	mov	rax, [rbp + gseg.process]
%if log_irq
	push	rsi
lodstr	rdi, 'recv delayed irq %x', 10
	call	printf
	pop	rsi
%endif
	zero	edi
	swapgs
	mov	rax, [rbp + gseg.process]
	mov	qword [rax + proc.rax], MSG_PULSE
	jmp	fastret.no_clear

syscall_call:
	mov	rax, [rbp + gseg.process]
	; saved rax: message code
	; rdi: target process
	; remaining: message params
	save_regs rdi,rsi,rdx,r8,r9,r10

.from_other:
	mov	rdi, [rax + proc.aspace]
	mov	rsi, [rax + proc.rdi]
	call	find_handle
	; A send must have a specific target
	test	rax, rax
	jz	.no_target
	; Put the target handle in proc's rdi
	mov	rsi, [rbp + gseg.process]
	mov	[rsi + proc.rdi], rax

%if log_messages
	push	rax
lodstr	rdi,	'%p: call through %x to %p (%x)', 10
	mov	rdx, [rax + handle.key]
	mov	rcx, [rax + handle.proc]
	mov	r8, [rax + handle.other]
	test	r8, r8
	jz	.no_other
	mov	r8, [r8 + handle.key]
.no_other:
	call	printf
	pop	rax
	mov	rsi, [rbp + gseg.process]
%endif

	; 1. Set IN_SEND and IN_RECV for this process
	or	[rsi + proc.flags], byte PROC_IN_SEND | PROC_IN_RECV
	mov	rdi, [rax + handle.proc]
	call	send_or_block
	mov	rax, [rbp + gseg.process]
	jmp	switch_to

.no_target:
	mov	rax, [rbp + gseg.process]
	swapgs
	jmp	fastret

; Returns unless it blocks.
; rdi: recipient process
; rsi: source (the process that will block if it can't send)
; Both will have a handle (or null) in proc.rdi
; rsi must be IN_SEND or IN_SEND|IN_RECV
; rsi will always be the currently running process.
; rdi might be in any state.
send_or_block:
	test	[rsi + proc.flags], byte PROC_IN_SEND
	jz	.not_in_send
	; Sending half:
	; ( TODO for multiprocessing: lock recipient process struct. And sender?)
	; 1. Check if the recipient can receive a message immediately
	mov	edx, [rdi + proc.flags]
	and	edx, PROC_IN_RECV | PROC_IN_SEND | PROC_PFAULT
	; If it's IN_RECV in combination with another status, that means we
	; should treat it as IN_SEND/PFAULT first and block.
	cmp	edx, byte PROC_IN_RECV
	jne	.block_on_send

	mov	rdx, [rsi + proc.rdi]
	mov	rax, [rdi + proc.rdi]
	test	rax, rax
	jz	.open_recv
	mov	rcx, [rax + handle.other]

	; other == null => receiving to fresh handle
	test	rcx, rcx
	jz	.do_transfer

	; non-null other => receiving to specific, and we must match
	cmp	rcx, rdx
	jne	.block_on_send

	jmp	.do_transfer

.open_recv:
%if log_messages
	push	rdi
	push	rsi
	mov	rdx, rsi
	mov	rsi, rdi
lodstr	rdi,	'Open receive %p <- %p', 10
	;call	printf

	pop	rsi
	pop	rdi
%endif

.do_transfer:

	; --- If it can (it's IN_RECV and not IN_SEND):
	; 2. Copy register contents from current process to recipient
	; 3. Reset recipient's IN_RECV and sender's IN_SEND flags
	; 4. Put recipient on runqueue (it'll "receive" its message by vestige
	; of its registers just having been modified by us)
	; 5. Check if it's on the waiter-queue for the sender - we'll have to
	; remove it.
	tcall	transfer_message

.not_in_send:
	PANIC

.block_on_send:
	; --- The recipient can't receive the message right now (not IN_RECV)
	; 2. Add it to waiting-for list on target process. This process remains
	; out of the run-queue until target starts receiving. We have to yield
	; and find another process to run.
	; 3. We probably need some state to say where we're sending to.
	; 4. Return and yield

	; rdi: target process
	; rbp + gseg.process: current/source process

%if log_messages
	mov	rdx, [rsi + proc.flags]
	mov	rcx, rdi
	mov	r8, [rcx + proc.flags]

	push	rsi
	push	rdi
lodstr	rdi,	'%p (%x) blocked on send to %p (%x)', 10
	call	printf
	pop	rdi
	pop	rsi
%endif
	; I think this code is not as complicated as it needs to be...
	; Or maybe it's more complicated...
	; rsi is always the previously running process when we get here
	btr	dword [rsi + proc.flags], PROC_RUNNING_BIT
	jnc	.sender_not_running
	call	add_to_waiters
	tcall	switch_next

.sender_not_running:
	PANIC

; Associate a pair of handles with each other. Assume that rcx (sender handle)
; is already pointing to rdi (recipient).
;
; rdi: target/recipient process
; rsi: sender/source process
; rdx: recipient handle object
; rcx: sender handle object
assoc_handles:
	; TODO Check that these start out as null
	mov	[rdx + handle.other], rcx
	mov	[rdx + handle.proc], rsi
	mov	[rcx + handle.other], rdx
	; TODO Check that sender handle's proc is indeed rdi

	; Finish adding handle to process
	mov	rsi, rdx
	tcall	proc_insert_handle

; rdi: target process
; rsi: handle object
; rdx: pulses to deliver
; Always returns to the caller, may unblock dst if it was waiting.
transfer_pulse:
	; pulses to deliver
	mov	[rdi + proc.rsi], rdx

	push	rdi
	push	rsi
	; process = null ... though we could get it from handle.other ->
	; handle.proc since we require previously associated handles, that
	; requirement also means that transfer_set_handle will never use the
	; process argument.
	zero	edx
	call	transfer_set_handle
	pop	rsi
	pop	rdi

	zero	eax
	mov	al, MSG_PULSE
	mov	[rdi + proc.rax], rax
	; TODO Refactoring: move the wake up into a common transfer function,
	; merge that with transfer_set_handle and run *after* copying/setting
	; message registers.
	; Both kinds of transfers become set registers -> tailcall
	and	[rdi + proc.flags], byte ~PROC_IN_RECV
	test	[rdi + proc.flags], byte PROC_RUNNING
	jnz	.ret
	mov	rsi, [rsi + handle.proc]
	xchg	rsi, rdi
	call	stop_waiting
.ret	ret

; rdi: target process
; rsi: source handle object (from sender! *not* the handle in the recipient)
; target's rdi is set to the appropriate handle key for a receive from rsi.
; no message registers are touched here. target *must* currently be blocked on
; receive.
; rdx: source process
; TODO Swap rsi and rdx here to match transfer_message
;
; It's assumed we have already checked that it's a valid receive match up (i.e
; either a fresh/open receive, or it's for the correct handle). Any checks here
; are just bonuses/asserts and will panic.
transfer_set_handle:
	push	rdx

	mov	rdx, [rdi + proc.rdi] ; recipient handle (object)
	test	rdx, rdx
	jz	.null_recipient_id
	mov	rax, [rdx + handle.other]
	test	rax, rax
	jz	.fresh_handle

	; not-fresh handle: check that the recipient did want a message from
	; *us*. If not, panic.
	cmp	rsi, rax
	push	qword [rdx + handle.key]
	pop	qword [rdi + proc.rdi]
	je	.rcpt_rdi_set

	; rbx = sender's handle
	; rdx = recipient's handle
	; rax = recipient's handle's other handle
%if log_messages
	mov	r8, rsi
lodstr	rdi, 'rcpt handle %p -> handle %p (proc %p) != sender handle %p', 10
	mov	rsi, rdx
	mov	rdx, rax
	mov	rcx, [rdx + handle.proc]
	call	printf
%endif

	; Mismatched sender/recipient.
	PANIC

.fresh_handle:
	; rdx = recipient's (fresh) handle
	; Get the source handle's other handle, i.e. the recipient handle.
	mov	rax, [rsi + handle.other]
	test	rax, rax
	; other is not null: a fresh-handle receive got fulfilled by a known
	; handle - return that handle instead as if we'd have given null.
	jnz	.junk_fresh_handle

	push	rdi
	push	rsi
	; rdi = recipient process
	; rsi = sender process
	; rdx = recipient handle
	; rcx = source handle
	mov	rcx, rsi
	mov	rsi, [rsp + 16]
	call	assoc_handles
	pop	rsi
	pop	rdi

	jmp	.null_recipient_id

.junk_fresh_handle:
	push	rdi
	push	rsi
	push	rdx

%if log_messages
lodstr	rdi, 'Junking fresh rcpt %x because sender %x has other %x', 10
	mov	rbx, rsi ; source handle
	mov	rsi, [rdx + handle.key]
	mov	rdx, [rbx + handle.key]
	mov	rcx, [rbx + handle.other]
	mov	rcx, [rcx + handle.key]
	call	printf
%endif

	; rdi = recipient handle object we would've used
	pop	rdi
	call	free_frame

	pop	rsi
	pop	rdi

.null_recipient_id:
	mov	rax, [rsi + handle.other]
	test	rax, rax
	jz	.rax_has_key
.rax_has_handle:
	; rax is the recipient-side handle - pick out its key directly
	mov	rax, [rax + handle.key]
.rax_has_key:
	mov	[rdi + proc.rdi], rax
.rcpt_rdi_set:
	; we don't care about restoring rdx as such, just balance the stack
	pop	rdx
	ret

; rdi: target process
; rsi: source process
; Note: we return from transfer_message if not blocked
transfer_message:
	; Find the source handle we were sending from
	mov	rbx, [rsi + proc.rdi]

	push	rdi
	push	rsi
	mov	rdx, rsi
	; note source handle, not source process
	mov	rsi, rbx
	call	transfer_set_handle

	mov	rsi, [rsp]
	mov	rdi, [rsp + 8]
	; [rsp] = source/sender process
	; [rsp+8] = target/recipient process

	; rcx = source handle object (assumed non-zero, because otherwise
	; there'd be no way for them to send anything!)

%macro copy_regs 0-*
%rep %0
	mov	rax, [rsi + proc. %+ %1]
	mov	[rdi + proc. %+ %1], rax
	%rotate 1
%endrep
%endmacro
	; If message-parameter registers were ordered in the file, we could
	; do a rep movsd here.
	; rdi is not copied - that's the sender's process handle to the
	; recipient process.
	copy_regs rax, rsi, rdx, r8, r9, r10

	and	[rdi + proc.flags], byte ~PROC_IN_RECV
%if log_messages
	mov	rdx, rdi
	mov	rcx, [rdi + proc.rdi]
	mov	r8, [rdi + proc.flags]
lodstr	rdi, 'Copied message from %p to %p (handle = %x, flags = %x)', 10
	call	printf
%endif
	; The recipient is guaranteed to be unblocked by this, make it stop
	; waiting.
	mov	rsi, [rsp + 8] ; recipient, the one being unblocked
	test	[rsi + proc.flags], byte PROC_RUNNING
	jnz	.not_blocked
	; Recipient was blocked - unblock it.
	mov	rdi, [rsp]
	call	stop_waiting
.not_blocked:

	pop	rdi
	pop	rsi
	; rsi = source/sender process
	; rdi = target/recipient process

	; If the previously-sending process is now receiving (was in sendrcv),
	; continue with its receiving phase. Otherwise unblock it.
	and	[rdi + proc.flags], byte ~PROC_IN_SEND
	test	[rdi + proc.flags], byte PROC_IN_RECV
	; Error: someone's proc.rdi got reset back to the local-address instead
	; of pointing to the handle object before we did this.
	; (It was rdi + proc.rdi)
	tc nz,	recv

%if log_messages
	push	rdi
	push	rsi
	mov	rsi, rdi
	mov	rdx, [rsi + proc.flags]
lodstr	rdi, '%p (%x) just sent, not sendrcv', 10
	call	printf
	pop	rdi
	pop	rsi
%else
	xchg	rsi,rdi
%endif

	; The sender was only sending - unblock it if necessary
	zero	eax
	mov	[rsi + proc.rdi], rax
	test	[rsi + proc.flags], byte PROC_RUNNING
	jnz	.sender_was_running

	; rdi: process that was being waited for
	; rsi: process that might need waking up
	tcall	stop_waiting

.sender_was_running:
	ret

; rdi: process that might have become able to receive
; rsi: process it wants to receive from (or null)
; If this does not block, it will return to the caller.
recv:
	test	rsi, rsi
	tc z,	recv_from_any
	; recipient has a specific handle, but the sender might be sending to
	; a different one.
	mov	rax, [rdi + proc.rdi]
	mov	rax, [rax + handle.other]
	test	rax, rax
	jz	.fresh_recv
.fresh_recv:
	test	[rsi + proc.flags], byte PROC_IN_SEND
	jz	.not_in_send
	cmp	rax, [rsi + proc.rdi]
	; correct handle => transfer and continue
	tc e,	transfer_message

.not_match:
%if log_messages
	push	rdi
	push	rsi
	mov	rcx, rsi
	mov	rdx, [rdi + proc.rdi]
	mov	rsi, rdi
	mov	r8, [rcx + proc.rdi]
lodstr	rdi, '%p blocked on receive from %x: mismatch (%p has %x)', 10
	call	printf
	pop	rdi
	pop	rsi
%else
	; Swap to match add_to_waiters parameters
	xchg	rsi, rdi
%endif
	jmp	.block

.not_in_send:
%if log_messages
	push	rdi
	push	rsi
	mov	rdx, rsi
	mov	rsi, rdi
lodstr	rdi, '%p blocked on receive from %p: not in send', 10
	call	printf
	pop	rdi
	pop	rsi
%else
	xchg	rsi, rdi
%endif

.block:
	; rsi = recipient, rdi = potential sender
	; (we have swapped since the entry point)

	; Make sure that rsi is no longer runnable and on the waiters list
	; for rdi. The return if it doesn't block thing means: if the process
	; we're blocking is not running we should return.

	; This code may get called as a tail call from transfer_message to
	; finish more message transfers, so we don't know if the recipient is
	; the current process (in a syscall_recv), or some other process.
	;
	; Can we assume that if the recipient is not running it was already
	; blocked on the sender?
	btr	dword [rsi + proc.flags], PROC_RUNNING_BIT
	; Not blocking the current process: we might be able to do more stuff,
	; so return to whoever called after adding to waiters.
	tc nc,	add_to_waiters
	; Blocking the current process: add to waiters and switch_next
.not_running:
	call	add_to_waiters
	tcall	switch_next

; Find any sender that's waiting to send something to this process. If one
; exists, transfer the message.
;
; Receiving half:
; 1. Check waiter queue, find the process we're receiving from, or any
; process that's in IN_SEND and sending to us.
; 2. If we found a process:
; 2.1. Remove it from waiter queue, it's no longer waiting.
; 2.2. Do transfer_message to it
;
; rdi: process that wants to receive
; Returns if something was received.
recv_from_any:
	mov	rax, [rdi + proc.waiters]
.loop:
	test	rax, rax
	jz	.no_senders
%if log_find_senders
	push	rdi
	push	rax
lodstr	rdi,	'recv: %p (%x)', 10
	lea	rsi, [rax - proc.node]
	call	print_proc
	pop	rax
	pop	rdi
%endif
	lea	rsi, [rax - proc.node]
	test	[rax - proc.node + proc.flags], byte PROC_IN_SEND
	tc nz,	transfer_message

	mov	rax, [rax + dlist_node.next]
	jmp	.loop

.no_senders:
%if log_find_senders
	push	rdi
	mov	rsi, rdi
lodstr	rdi,	'No senders found to %p (%x)', 10
	mov	rdx, [rsi + proc.flags]
	call	printf
	pop	rdi
%endif

	push	rdi
	mov	rdi, [rdi + proc.aspace]
	call	get_pending_handle
	pop	rdi
	test	rax, rax
	jz	.no_pulses

	; TODO transfer_pulse Should take the the recipient's handle instead
	mov	rsi, [rdx + handle.other]
	mov	rdx, rax
	; rdi = recipient process
	; rsi = sender's handle object
	; rdx = pulses
	tcall	transfer_pulse

.no_pulses:
	tcall	block_and_switch

; rdi = handle to map
; rsi = flags
; rdx = virtual address
; r8 = offset in object
; r9 = size of mapping
syscall_map:
	mov	rax, [rbp + gseg.process]
	save_regs rdi,rsi,rdx,r8,r9

	mov	rbx, rax
	; (Optionally) release any pages faulted-in in this range.
	; mov	rdi, [rax + proc.aspace]
	; mov	rsi, [rax + proc.rdx]
	; mov	rdx, [rax + proc.r9]
	; call	unback_range

	and	qword [rbx + proc.rsi], byte MAPFLAG_USER_ALLOWED

	; region = null (in case we skip the allocating region bit because we're mapping a handle)
	zero	esi
	cmp	qword [rbx + proc.rdi], byte 0
	jnz	.not_dma

	; With handle = 0, the flags can be:
	; phys: raw physical memory mapping
	; anon: anonymous memory mapped on use
	; anon|phys: similar to anonymous, but the backing is allocated
	; immediately, the memory is "locked" (actually all allocations are),
	; and the phys. address of the memory (always a single page) is
	; returned in rax.

	test	byte [rbx + proc.rsi], MAPFLAG_PHYS
	jz	.not_dma
	; phys flag set => either raw phys or DMA mapping
	test	byte [rbx + proc.rsi], MAPFLAG_ANON
	jz	.not_dma
	; Both phys and anon => DMA mapping

.map_dma:
	; If this is a DMA region, allocate the memory immediately so that we
	; can return its physical address to the caller.
	; TODO "Unback" the page first, if it is backed.
	call	allocate_frame
	lea	rax, [rax - kernel_base]
	mov	[rbx + proc.r8], rax

.not_dma:
	mov	rdi, [rbx + proc.aspace]
	; rsi/input rdx: starting (virtual) address
	mov	rsi, [rbx + proc.rdx]
	; rdx: end address
	mov	rdx, rsi
	add	rdx, [rbx + proc.r9] ; input r9 = size of mapping
	; r8: flags (input rsi) | offset (input r8 - input rdx)
	mov	r8, [rbx + proc.r8]
	sub	r8, rsi
	or	r8, [rbx + proc.rsi]
	; rcx/input rdi: handle to map
	mov	rcx, [rbx + proc.rdi]
	call	map_range

	test	[rbx + proc.rsi], byte MAPFLAG_PHYS
	jnz	.dma_ret
	zero	eax
	ret
.dma_ret:
	mov	rax, [rbx + proc.r8]
	ret

; rdi = 0
; rsi = vaddr
; rdx = flags
syscall_pfault:
	; limit flags appropriately
	and	edx, MAPFLAG_RWX
	and	si, ~0xfff
	; set fault_addr to offset | flags
	mov	rax, [rbp + gseg.process]
	mov	[rax + proc.fault_addr], rsi
	mov	[rax + proc.rsi], rsi ; vaddr, but soon to become offset
	mov	[rax + proc.rdx], rdx
	; look up mapping at offset
	mov	rdi, [rax + proc.aspace]
	; rsi = vaddr already
	call	aspace_find_mapcard
	test	rax, rax
	jz	.panic
	mov	rcx, rax
	mov	rsi, [rax + mapcard.offset]
	mov	rax, [rbp + gseg.process]
	; * translate vaddr to offset => proc.rsi
	add	[rax + proc.rsi], rsi ; proc.rsi is now translated into offset
	; * get handle => proc.rdi
	mov	rsi, [rcx + mapcard.handle]
	test	rsi, rsi
	jz	.no_op_ret
	mov	[rax + proc.rdi], rsi
	; set PROC_PFAULT flag
	or	byte [rax + proc.flags], PROC_PFAULT
	; do the equivalent of sendrcv with rdi=handle, rsi=offset, rdx=flags
	; (special calling convention: rax = process)
	tcall	syscall_call.from_other
.panic:
	PANIC
.no_op_ret:
	zero	eax
	ret

; rdi = recipient handle
; rsi = our vaddr
; rdx = our flags
syscall_grant:
	mov	rax, [rbp + gseg.process]
	and	edx, MAPFLAG_RWX
	save_regs	rsi, rdi, rdx
	; lookup handle, get process
	mov	rdi, [rax + proc.aspace]
	mov	rsi, [rax + proc.rdi]
	call	find_handle
	test	rax, rax
	jz	.err_ret

	mov	rbx, [rax + handle.proc]
	test	rbx, rbx
	jz	.err_ret
	cmp	qword [rax + handle.other], 0
	jz	.err_ret
	; check that it has PROC_PFAULT set
	test	byte [rbx + proc.flags], PROC_PFAULT
	jz	.err_ret
	; get the process' fault address, look up the mapping
	push	rax
	mov	rsi, [rbx + proc.fault_addr]
	mov	rdi, [rbx + proc.aspace]
	call	aspace_find_mapcard
	; check that our handle's remote handle's key matched the one in the
	; mapping
	pop	rcx
	mov	rdx, [rax + mapcard.handle]
	mov	rcx, [rcx + handle.other]
	cmp	rdx, [rcx + handle.key]
	jne	.err_ret
	mov	al, [rax + mapcard.flags]
	and	al, MAPFLAG_RWX
	mov	rsi, [rbp + gseg.process]
	and	[rsi + proc.rdx], al

	; check that our offset matches what it should? we'd need to pass on
	; the offset that we think we're granting, and compare that to the
	; vaddr+offset on the recipient side....

	; proc.rdi = our handle
	; proc.rsi = our vaddr
	; proc.rdx = our flags (& MAPFLAG_RWX)
	; rbx + proc.fault_addr = their vaddr (+ flags?)

	; look up our backing for our-vaddr
	mov	rdi, [rsi + proc.aspace]
	mov	rsi, [rsi + proc.rsi]
	call	aspace_find_backing
	; extract the physical address or fault recursively
	test	rax, rax
	jz	.recursive_fault
	mov	rsi, [rax + backing.parent]
	test	byte [rax + backing.vaddr], MAPFLAG_PHYS
	jnz	.parent_was_paddr
	mov	rsi, [rsi + sharing.paddr]
.parent_was_paddr:
	push	rsi

	; (look up recipient's backing for rcpt-vaddr, release it)
	; look up or create a sharing for our-vaddr
	mov	rsi, [rbp + gseg.process]
	mov	rdi, [rsi + proc.aspace]
	mov	rsi, [rsi + proc.rsi]
	call	aspace_add_sharing
	pop	qword [rax + sharing.paddr]
	; create new backing, link it into the sharing we made
	mov	rdi, [rbx + proc.aspace]
	mov	rsi, [rbx + proc.fault_addr]
	mov	rdx, rax
	mov	rax, [rbp + gseg.process]
	or	si, word [rax + proc.rdx]
	; add flags (our flags & mapcard.flags) to rsi
	call	aspace_add_shared_backing

	btr	dword [rbx + proc.flags], PROC_PFAULT_BIT
	test	byte [rbx + proc.flags], PROC_IN_RECV
	jz	.unblock
	; process has PROC_IN_RECV, we got here from an explicit pfault,
	; do a send to respond properly.
	mov	rax, [rbp + gseg.process]
	mov	rdi, [rax + proc.rdi]
	tcall	syscall_send.from_other
.unblock
	; unblock the process
.err_ret:
	zero	eax
	;ret
.recursive_fault:
	; unimplemented...
.panic:
	PANIC

; rdi: handle key
; rsi: pulses to pulse
syscall_pulse:
%if log_pulses
	push	rsi
	push	rdi
	mov	rdx, rdi
lodstr	rdi, 'Pulsing %x of %x', 10
	call	printf
	pop	rdi
	pop	rsi
%endif

	push	rsi
	mov	rsi, rdi
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	call	find_handle

%if log_pulses
	push	rax
lodstr	rdi, 'Pulsing %x of %x', 10
	mov	rsi, [rsp + 8]
	mov	rdx, rax
	call	printf
	pop	rax
%endif

	pop	rdx

	test	rax, rax
	jz	.ret

	mov	rsi, [rax + handle.other]
	test	rsi, rsi
	jz	.ret

	mov	rdi, [rax + handle.proc]
	test	rdi, rdi
	jz	.ret
	push	rdi
	push	rax
	mov	rdi, [rdi + proc.aspace]
	call	pulse_handle

	pop	rdx ; the pulsed handle (*our* handle)
	pop	rdi ; recipient process
	mov	al, [rdi + proc.flags]
	and	al, PROC_IN_RECV | PROC_IN_SEND | PROC_PFAULT
	cmp	al, PROC_IN_RECV
	jne	.ret0

	; Get the recipient handle that the other process is waiting for
	mov	rsi, [rdi + proc.rdi]
	; Open-ended receive: deliver
	test	rsi, rsi
	jz	.can_deliver

	mov	rax, [rsi + handle.other]
	; Receive to fresh handle: deliver freely
	test	rax, rax
	jz	.can_deliver
	; Receive to specific: if it's not the right handle, we can't deliver
	; now.
	cmp	rax, rdx
	jne	.ret0

.can_deliver:
	; sender's handle
	; TODO Swap rsi,rdx above to get rid of this one.
	mov	rsi, rdx

	; FIXME Should use get_pulse etc to unqueue the handle
	zero	rdx
	mov	rcx, [rsi + handle.other]
	xchg	rdx, [rcx + handle.events]

	; rdi = recipient process
	; rsi = sender's handle object
	; rdx = pulses
	call	transfer_pulse

.ret0	zero	eax
.ret	ret

%include "printf.asm"

panic_print:
lodstr	rdi, 'PANIC @ %x', 10 ; Decimal output would be nice...
	call	printf
	cli
	hlt

section .rodata

%macro vga_string 1-*
	%assign color %1
	%rotate 1
%rep (%0 - 1)
	db %1, color
	%rotate 1
%endrep
%endmacro
message:
	vga_string 7,'L','O','N','G','M','O','D','E'

section .text
tss:
	dd 0 ; Reserved
	; Interrupt stack when interrupting non-kernel code and moving to CPL 0
	dq phys_vaddr(kernel_stack_end)
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

section .rodata
align	4
gdtr:
	dw	gdt_end-gdt_start-1 ; Limit
	dq	text_vpaddr(gdt_start)  ; Offset

section .rodata
idtr:
	dw	idt_end-idt-1
	dq	bss_vpaddr(idt)
idt_data:
.vectors:
%macro reg_vec 2
	db	%1
	dw	%2 - text_vstart_dummy
%if idt_nvec <= %1
%assign idt_nvec %1 + 1
%endif
%endmacro
%assign idt_nvec 0
	reg_vec 7, handler_NM
	;reg_vec 8, handler_DF
	reg_vec 14, handler_PF
	; 32..47: PIC interrupts
	; 48: APIC timer interrupt
%assign idt_nvec 32
%rep 17
	reg_vec idt_nvec, handle_irq_ %+ idt_nvec
%endrep
.vectors_end:

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

mbi_pointer resd 1
memory_start resd 1

; Provide room for all possible interrupts, although we'll only use up to
; 48 or so
idt	resq 2 * idt_nvec
idt_end:

section.bss.end:

section .rodata
section.data.end:

