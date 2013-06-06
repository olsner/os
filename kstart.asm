[map all kstart.map]

; Configuration
%define log_switch_to 0
%define log_switch_next 0
%define log_idle 0
%define log_runqueue 0
%define log_runqueue_panic 0
%define log_fpu_switch 0 ; Note: may clobber user registers if activated :)
%define log_timer_interrupt 0
%define log_page_fault 0
%define log_find_mapping 0
%define log_find_handle 0
%define log_new_handle 0
%define log_hmod 0
%define log_find_senders 0
%define log_mappings 0
%define log_waiters 0
%define log_messages 0

%define builtin_keyboard 1
%define builtin_timer 1
%define bochs_con 1

%define debug_tcalls 0

%assign need_print_procstate (log_switch_to | log_switch_next | log_runqueue | log_runqueue_panic | log_waiters | log_find_senders | log_timer_interrupt)

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
%macro PANIC_ 1-2 'PANIC'
	mov	esi, %1
	jmp	panic_print
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
%include "handles.inc"
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
	; bitmask of irqs that have been delayed (extend to per-irq counter?)
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
mboot_load \
	text_paddr(mboot_header), \
	section..text.vstart, \
	section.data.end, \
	kernel_reserved_end, \
	text_paddr(start32_mboot)
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
	mov	[pages.pml4], dword 0
	mov	[pages.kernel_pdp + 0xff0], dword pages.low_pd | 3
	mov	[pages.low_pd + 0xff8], dword pages.low_pt | 3
	; Set page kernel_base-0x1000 to uncacheable and map it to the APIC
	; address.
	mov	[pages.low_pt + 0xff8], dword APIC_PBASE | 0x13
	invlpg	[rel -0x1000]

apic_setup:
	mov	ecx, MSR_APIC_BASE
	rdmsr
	; Clear high part of base address
	zero	edx
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

%assign APIC_TIMER_IRQ 0x30

%assign rbpoffset 0x380

	mov	rbp,kernel_base-0x1000+rbpoffset
	mov	ax,1010b
	mov	dword [rbp+APIC_REG_TIMER_DIV-rbpoffset],eax  ; Divide by 128

%if builtin_timer
	mov	dword [rbp+APIC_REG_TIMER_LVT-rbpoffset], 0x20000 + APIC_TIMER_IRQ
%else
	mov	dword [rbp+APIC_REG_TIMER_LVT-rbpoffset], 0x10000
%endif
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

test_alloc:
	zero	ebx
	zero	r12
.loop:
	call	allocate_frame
	test	rax,rax
	jz	.done
	inc	rbx
	mov	[rax], r12
	mov	r12, rax
	jmp	.loop

.done:
lodstr	rdi,	'%x frames allocated (memory_start %x)', 10
	mov	rsi, rbx
	mov	edx, [memory_start]
	call	printf

test_free:
	mov	rdi, r12
	test	rdi, rdi
	jz	.done
	mov	r12, [rdi]
	call	free_frame
	jmp	test_free

.done:

%if builtin_keyboard
init_keyboard:
lodstr	rdi,	'Enable keyboard', 10
	call	printf

	mov	al,0xff ^ 2 ; Enable IRQ 1 (keyboard)
	out	PIC1_DATA, al
%endif

fpu_initstate:
	call	allocate_frame
	o64 fxsave [rax]
	mov	[rel globals.initial_fpstate], rax

	; Make the first use of fpu/multimedia instructions cause an exception
	mov	rax,cr0
	bts	rax,CR0_TS_BIT
	mov	cr0,rax

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
	mov	rsi, rdi
	push	rsi
lodstr	rdi,	'done. first process is %p', 10
	call	printf

	; Set up the pointer to the dedicated IRQ process
	pop	qword [rbp + gseg.irq_process]

	jmp	switch_next

handle_irq_generic:
	; we have an irq number in bl, user-space rbx on stack
	; all other state undefined
	push	rax
	push	rbp
	swapgs

	mov	word [rel 0xb8002], 0x0700|'Q'

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
	add	rax, [rbp + gseg.process]
	jz	.no_save
	; The rax and rbp we saved above, store them in process
	pop	qword [rax + proc.rbp]
	pop	qword [rax + proc.rax]
	movzx	ebx, bl ; not necessary, but nice
	push	rbx ; interrupt number
	; save_from_iret will save the wrong rbx now - we'll resave the right
	; one below.
	call	save_from_iret

	mov	rdi, [rbp + gseg.process]
	pop	rbx ; interrupt number
	pop	qword [rdi + proc.rbx]
	mov	qword [rbp + gseg.process], 0 ; some kind of temporary code
	call	runqueue_append
	jmp	.saved
.no_save:
	; Pop the saved rbx/rax/rbp, plus the 5-word interrupt stack frame
	add	rsp, 8 * 8
.saved:

lodstr	rdi,	'handle_irq_generic: %x', 10
	mov	rsi, rbx
	call	printf

	; Not very nice to duplicate message-sending here...
	; Oh, and we forget to check some things:
	; - the receiver has to be in an open-ended (fresh or null) receive,
	;   otherwise we might accidentally "respond" to an in-progress call,
	;   which will now get dropped on the floor.
	; - (other things too, probably)
	mov	rax, [rbp + gseg.irq_process]
	test	[rax + proc.flags], byte PROC_IN_SEND
	jnz	.delay
	test	[rax + proc.flags], byte PROC_IN_RECV
	jz	.delay

	and	[rax + proc.flags], byte ~PROC_IN_RECV
	mov	qword [rax + proc.rax], 0
	mov	[rax + proc.rsi], ebx
	mov	qword [rax + proc.rax], msg_send(MSG_IRQ_T)

	jmp	switch_to
.delay:
lodstr	rdi,	'handle_irq_generic: delivery delayed', 10
	mov	rsi, rbx
	call	printf

	bts	dword [rbp + gseg.irq_delayed], ebx
	jmp	switch_next

%macro handle_irqN_generic 1
handle_irq_ %+ %1:
	push	rbx
	mov	bl, %1
	jmp	handle_irq_generic
%endmacro

%assign irq 32
%rep 16
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

launch_user:
lodstr	rdi,	'Loading module %p..%p', 10
	mov	rsi, r13
	mov	rdx, r14
	call	printf

	mov	rdi, r13
	mov	rsi, r14

; Set up a new process with some "sane" defaults for testing, and add it to the
; run queue.
;
; rdi: user_entry (start of module)
; rsi: end of module

	; Round up end-of-module
	add	rsi, 0xfff
	and	si, 0xf000
	mov	r12, rdi
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

	; Map module at 1MB
	call	allocate_frame ; TODO allocate region, not whole page :)
	; vaddr, vsize
	mov	rcx, r13 ; end of module (phys.)
	sub	rcx, r12 ; substract start => length of module
	mov	[rax + region.paddr], r12 ; start of module (phys.)
	mov	[rax + region.size], rcx
	mov	edx, 1 << 20 ; vaddr
	mov	rdi, [rbx + proc.aspace]
	mov	rsi, rax
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_X

	; Map the user stack as an allocate-on-use ("anon") page, located just
	; below the module start, where we pointed rsp.
	mov	rdi, [rbx + proc.aspace]
	zero	esi
	mov	edx, (1 << 20) - 0x1000
	mov	ecx, 0x1000 ; stack size
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_W | MAPFLAG_ANON

	mov	rdi, rbx
	push	rdi
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

	mov	qword [rsi + proc.waiting_for], 0
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
	test	dword [rdi + proc.flags], PROC_IN_SEND | PROC_IN_RECV
	jne	.queueing_blocked
	bts	dword [rdi + proc.flags], PROC_ON_RUNQUEUE_BIT
	jc	.ret ; already on runqueue
	lea	rsi, [rdi + proc.node]
	lea	rdi, [rbp + gseg.runqueue]
	call	dlist_append
.ret:
%if log_runqueue
	call	print_procstate
%endif
	ret
.queueing_blocked:
	mov	rsi, rdi
lodstr	rdi, 'queueing blocked %p (%x)', 10
	jmp	.panic
.already_running:
	mov	rsi, rdi
lodstr	rdi, 'queueing already running %p (%x)', 10
.panic:
%if log_runqueue_panic
	call	print_proc
	call	print_procstate
%else
	mov	rdx, [rsi + proc.flags]
	call	printf
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
	cmp	qword [rbp + gseg.process], 0
	jnz	.panic
	; Fun things to do while idle: check how soon the first timer wants to
	; run, make sure the APIC timer doesn't trigger before then.
	swapgs
	sti
	hlt
.panic:
	; We should never get here: the interrupt handler(s) will return through
	; the scheduler which will just re-idle if needed.
	PANIC

block_and_switch:
	btr	dword [rdi + proc.flags], PROC_RUNNING_BIT
	jnc	switch_next
	mov	qword [rbp + gseg.process], 0

switch_next:
%if log_switch_next
	mov	r12, rax
	mov	r13, rdx
lodstr	rdi, 'switch_next', 10
	call	printf
	call	print_procstate
	mov	rax, r12
	mov	rdx, r13
%endif

	call	runqueue_pop
	test	rax, rax
	jz	idle
	tcall	switch_to

%if need_print_procstate
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
%endif ;need_print_procstate

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

	mov	rbx, [rax + proc.flags]
	bt	rbx, PROC_KERNEL_BIT
	jc	.kernel_exit

.user_exit:
	; If we stop disabling interrupts above, this will be wildly unsafe.
	; For now, we rely on the flags-restoring part below to atomically
	; restore flags and go to user mode. The risk is if we switch "from
	; kernel" while having the user GS loaded!
	swapgs
	bt	rbx, PROC_FASTRET_BIT
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

.kernel_exit:
	hlt
	; Exit to kernel thread
	; If we don't need to switch rsp this should be easier - restore all
	; regs, rflags, push new rip and do a near return
	push	data64_seg
	push	qword [rbx+proc.rsp]
	push	qword [rbx+proc.rflags]
	push	code64_seg
	jmp	.restore_and_iretq

.ret_no_intrs:
	cli
	hlt

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

	mov	rsi, rax
	add	rsi, 128
	mov	ecx, 16
	; Clear the task-switched flag while we reuse some registers
	mov	rdx, cr0
	clts
	movdqu	[rbp + gseg.temp_xmm0], xmm0
	xorps	xmm0, xmm0
.loop:
%assign i -128
%rep 16
	movntdq	[rsi + i], xmm0
%assign i i+16
%endrep
	add	rsi, 16*16
	loop	.loop
	movdqu	xmm0, [rbp + gseg.temp_xmm0]
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
	.proc	resq 1
	; pointer to other handle if any. Its 'key' field is the other-name that
	; we need when e.g. sending it a message. If null this is not associated
	; in other-proc yet.
	.other	resq 1
endstruc
handle.key equ handle.dnode + dict_node.key

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
MAPFLAG_HANDLE	equ 0x10000 ; deprecated

; A non-present page in the page table with this flag set is a handle
PT_FLAG_HANDLE	equ 0x2

; A mapping is a mapped virtual memory range, sometimes backed by a region,
; other times just a placeholder for virtual memory that may become a region by
; allocating/loading-from-disk/whatever the frames that would be required.
struc mapping
	.owner	resq 1 ; address_space
	.region	resq 1
	.kobj	equ .region ; deprecated
	.flags	resq 1
	; Offset into region that corresponds to the first page mapped here
	.reg_offset resq 1
	.vaddr	resq 1
	.size	resq 1
	; Links for region.mappings
	.reg_node	restruc dlist_node
	; Links for aspace.mappings
	.as_node	restruc dlist_node
endstruc

struc aspace
	; Upon setup, pml4 is set to a freshly allocated frame that is empty
	; except for the mapping to the kernel memory area (which, as long as
	; it's less than 4TB is only a single entry in the PML4).
	.pml4		resq 1
	; TODO Lock structure for multiprocessing
	.count		resd 1
	.flags		resd 1
	; Do we need a list of processes that share an address space?
	;.procs	resq 1
	.mappings	restruc dlist
	; Lowest handle used. The range may contain holes. Something clever
	; similar to how we (will) handle noncontiguous mappings will apply :)
	; Also, all handles have mappings.
	.handles_bottom	resq 1
	.handles	restruc dict
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
	mov	[rax + mapping.vaddr], rdx
	mov	[rax + mapping.size], rcx

	;mov	r9, rdi
	mov	r10, rsi
	lea	rdi, [rdi + aspace.mappings]
	lea	rsi, [rax + mapping.as_node]
	call	dlist_append
	mov	rdi, rax ; Mapping
	mov	rsi, r10 ; Region

.add:
	mov	[rdi + mapping.region], rsi
	mov	rax, rdi

	test	rsi, rsi
	jz	.no_region
	lea	rsi, [rsi + region.mappings]
	lea	rdi, [rdi + mapping.reg_node]
	xchg	rsi, rdi
	call	dlist_append

	inc	dword [rsi + region.count]
.no_region:

.oom:
	ret
map_add_region equ .add

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
	tc nz,	free_frame
	ret

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
	and	cx, 0xff8 ; Then 'and' away the boring bits
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

	test	qword [r12], 1
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
	test	qword [r12], 1
	; We should probably check and handle mapping the same page twice as a
	; no-op. Or maybe not - since it indicates other code tried to map
	; something - getting a fault for an already-mapped page might mean the
	; user code is getting an actual fault of some kind and should be
	; killed insted.
	jnz	.panic

	pop	rdx
	pop	rsi
	mov	[r12], rsi
%if log_mappings
lodstr	rdi, 'Mapping %p to %p (at %p)!', 10
mapping_page_to_frame equ _STR
	mov	rcx, r12
	call	printf
%endif
	pop	r12

	ret

.panic:
	cli
	hlt

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

%if builtin_keyboard
key_handler:
	push	rax
	push	rbp
	swapgs

	mov	word [rel 0xb8002], 0x0700|'K'

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
	add	rax, [rbp + gseg.process]
	jz	.no_save
	; The rax and rbp we saved above, store them in process
	pop	qword [rax + proc.rbp]
	pop	qword [rax + proc.rax]
	call	save_from_iret

	mov	rdi, [rbp + gseg.process]
	mov	qword [rbp + gseg.process], 0 ; some kind of temporary code
	call	runqueue_append
	jmp	.saved
.no_save:
	; Pop the saved rax/rbp, plus the 5-word interrupt stack frame
	add	rsp, 7 * 8
.saved:

KEY_DATA	equ 0x60

	zero	eax
	in	al, KEY_DATA

lodstr	rdi,	'Key interrupt! key %x', 10
	mov	esi, eax
	call	printf

	mov	al, PIC_EOI
	out	PIC1_CMD, al

	jmp	switch_next
%endif

%if builtin_timer
timer_handler:
	push	rax
	push	rbp
	swapgs

	mov	word [rel 0xb8002], 0x0700|'T'

	zero	eax
	mov	rbp, [gs:rax + gseg.self]
	inc	dword [rbp + gseg.curtime]
	mov	eax, dword [rel -0x1000 + APIC_REG_APICTCC]
	mov	dword [rbp + gseg.tick], eax
	mov	dword [rel -0x1000 + APIC_REG_APICTIC], APIC_TICKS
	mov	dword [rel -0x1000 + APIC_REG_EOI], 0

	mov	rax, [rbp + gseg.process]
	test	rax, rax
	jnz	.save

	; Pop the saved rax/rbp, plus the 5-word interrupt stack frame
	add	rsp, 7 * 8
%if log_timer_interrupt
lodstr	rdi, 'Timer interrupt while idle', 10
	call	printf
%endif
	jmp	switch_next

.save:
	; The rax and rbp we saved above, store them in process
	pop	qword [rax + proc.rbp]
	pop	qword [rax + proc.rax]
	call	save_from_iret

%if log_timer_interrupt
lodstr	rdi, 'Timer interrupt in %p (%x)', 10
	mov	rsi, [rbp + gseg.process]
	call	print_proc
%endif
	mov	rdi, [rbp + gseg.process]
	mov	qword [rbp + gseg.process], 0 ; some kind of temporary code
	call	runqueue_append
%if log_timer_interrupt > 1
lodstr	rdi, 'Next proc %p (%x)', 10
	mov	rsi, [rbp + gseg.runqueue + dlist.head]
	sub	rsi, proc.node
	call	print_proc
%endif
	jmp	switch_next
%endif

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
; Return mapping in rax if successful
aspace_find_mapping:
%if log_find_mapping
	push	rbp
	push	rdi
	push	rsi
	mov	rdx, rdi
lodstr	rdi, 'Map find %x in aspace %p', 10
	call	printf
	pop	rsi
	pop	rdi
	pop	rbp
%endif
	mov	rdi, [rdi + aspace.mappings + dlist.head]
.test_mapping:
	test	rdi, rdi
	jz	.no_match
	push	rdi
	push	rsi
	mov	rsi, rdi
	mov	rdx, [rdi + mapping.vaddr - mapping.as_node]
	mov	rcx, [rdi + mapping.size - mapping.as_node]
	mov	r8, [rdi + dlist_node.prev]
	mov	r9, [rdi + dlist_node.next]
%if log_find_mapping
lodstr	rdi, 'Map %p: %p sz %x (%p<-->%p) vaddr %x', 10
	call	printf
%endif
	pop	rsi
	pop	rdi
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
	zero	eax
	ret
.found:
	lea	rax, [rdi - mapping.as_node]
	ret

handler_PF:
	test	byte [rsp], 0x4
	jz	.kernel_fault

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
lodstr	rdi,	'Page-fault: cr2=%p error=%x proc=%p', 10
	mov	rsi, cr2
	; Fault
	mov	rdx, [rsp]
	mov	rcx, rax
	call printf
%endif

	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rsi, cr2
	call aspace_find_mapping
	test	rax, rax
	jnz	.found
.no_match:
lodstr	r12,	'No mapping found!', 10
.invalid_match:
%if log_page_fault
	mov	rdi, r12
	call	printf
%endif
	cli
	hlt
.found:
	lea	rdx, [rdi - mapping.as_node]
	mov	rcx, [rdi + mapping.vaddr - mapping.as_node]
	mov	r12, rdx
%if log_mappings
lodstr	rdi,	'Mapping found:', 10, 'cr2=%p map=%p vaddr=%p', 10
	call	printf
%endif

	; rsi is fault address (though we already know which mapping we're
	; dealing with)
	mov	rsi, cr2
	mov	rdi, r12 ; Doesn't need offsetting by mapping.as_node anymore

	mov	rax, [rdi + mapping.flags]
	mov	rsi, rax
	test	eax, MAPFLAG_HANDLE
lodstr	r12, 'Memory-access to handle', 10
	jnz	.invalid_match ; Handles can never be accessed as memory
	test	eax, MAPFLAG_RWX
lodstr	r12, 'Access to page without access (%p)', 10
	jz	.invalid_match ; We had none of the read/write/execute permissions

	; Was the access a write access? (bit 1: set == write)
	test	byte [rsp], 0x2
	jz	.not_a_write
	; Write to a read-only page: fault
	test	eax, MAPFLAG_W
lodstr	r12, 'Write access to read-only page', 10
	jz	.invalid_match
	; If page is "writeable" but also CoW, we need more handling here
.not_a_write:
	; TODO Check for access-during-instruction-fetch for a present NX page


	test	eax, MAPFLAG_ANON
	jz	.map_region

	; If anonymous, but a region has already been set up, use that region
	cmp	qword [rdi + mapping.region], 0
	jnz	.map_region

	mov	r12, rdi

	; Anonymous mapping: allocate a fresh zero page, create a region for
	; it, link the mapping to that region and fall through to adding the
	; new region to the page table.
	call	allocate_frame
	lea	rbx, [rax - kernel_base]
	call	allocate_frame
	mov	[rax + region.paddr], rbx
	mov	word [rax + region.size], 0x1000
	;mov	[rax + region.flags], REGFLAG_COW
	mov	rdi, r12 ; mapping
	mov	rsi, rax ; region
	call	map_add_region

	mov	rdi, r12 ; mapping

.map_region
	; 1. The region has a physical page backing it already:
	;   * Check that we are allowed to map it as it is (i.e. correct
	;     permissions, no CoW required, etc)
	;   * Add all required intermediate page table levels
	;   * When reaching the bottom: add a mapping with the right parameters

	mov	rsi, [rdi + mapping.region]
	test	rsi, rsi
lodstr	r12, 'Mapping something without a region', 10
	jz	.invalid_match

	mov	r9, cr2
	and	r9w, ~0xfff
	sub	r9, [rdi + mapping.vaddr]
	mov	r8, [rsi + region.paddr]
	add	r8, [rdi + mapping.reg_offset]
	add	r8, r9
	; r8: page frame to add to page table

	mov	eax, [rdi + mapping.flags]
	mov	rsi, r8
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
	test	eax, MAPFLAG_W
	jz	.no_write_access
	or	rsi, 2
.no_write_access:
	; test	eax, MAPFLAG_X
	; jnz	.has_exec
	; bts	rsi, 63
; .has_exec:
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rdx, cr2 ; Note: lower 12 bits will be ignored automatically
	call	add_pte

	; TODO Handle failures by killing the process.

.ret:
	mov	rax, [rbp + gseg.process]
	jmp	switch_to

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
lodstr	rdi, 'Invalid syscall %x!', 10
	mov	rsi, rbx
	call	printf
	jmp	.ret_from_generic

%macro sc 1
	dd	syscall_ %+ %1 - syscall_entry
%endmacro
.table
	sc recv
	sc nosys ; MAP
	sc nosys ; PFAULT
	sc nosys ; UNMAP
	sc hmod ; HMOD
	sc newproc
	; backdoor syscalls
	sc write
.end_table
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
	mov	ecx, 5
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

lodstr	rdi, 'dup_handle: %p (key=%x proc=%p)', 10
	mov	rsi, rax
	mov	rdx, [rax + handle.key]
	mov	rcx, [rax + handle.proc]
	call	printf

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
	test	rsi, rsi
	jz	.just_delete

	call	map_handle
	mov	[rax + handle.other], rbx
.just_delete:
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

	; changed some parameters, should redo register assignment here...
	mov	rdi, rsi
	mov	rsi, rdx

	; stack in new process = 1MB
	mov	esi, 0x100000
	; entry-point in new process = 1MB + (entry & 0xfff)
	and	edi, 0xfff
	add	edi, esi
	call	new_proc
	mov	rbx, rax ; Save new process id

	; Find mapping for source
	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	mov	rsi, r12
	call	aspace_find_mapping
	test	rax, rax
	jz	.panic
	; Set up to map the same region in the child process, at 1MB
	mov	rdi, [rbx + proc.aspace]
	mov	rsi, [rax + mapping.region]
	mov	edx, 0x100000
	; Set rcx to end, aligned up towards an even page
	mov	rcx, r13
	add	rcx, 0xfff
	and	cx, 0xf000
	; Then subtract start, and align upwards again
	sub	rcx, r12
	add	rcx, 0xfff
	and	cx, 0xf000
	; Check that we actually got a region
	test	rsi, rsi
	jz	.panic
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_X

	; Create new stack region for child
	mov	rdi, [rbx + proc.aspace]
	zero	esi
	mov	edx, 0x100000 - 0x1000 ; 1MB - stack size
	mov	ecx, 0x1000 ; stack size
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_W | MAPFLAG_ANON

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
lodstr	rdi,	'%p send via %p to %x (%p)', 10
	mov	rsi, [rbp + gseg.process]
	mov	rdx, rax
	mov	rcx, [rax + handle.key]
	mov	r8, [rax + handle.proc]
	call	printf
	pop	rax
%endif

	mov	rsi, [rbp + gseg.process]
	or	[rsi + proc.flags], byte PROC_IN_SEND
	mov	[rsi + proc.rdi], rax
	mov	rdi, [rax + handle.proc]
	; rdi: source *handle* for target process
	; rsi: source *process*
	call	send_or_block
	mov	rax, [rbp + gseg.process]
	swapgs
	jmp	fastret

.no_target:
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

%if log_messages == 0
	jz	.do_recv
%else
	jnz	.have_handle
	push	rax
lodstr	rdi, '%p: recv from any', 10
	mov	rsi, rax
	call	printf
	pop	rax
	jmp	.do_recv
.have_handle:
%endif

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

	call	allocate_frame
	mov	rdi, [rbp + gseg.process]
	mov	rdx, [rdi + proc.rdi]
	mov	[rax + handle.key], rdx
	zero	esi
	; rax = handle, rdi = receiving process
	jmp	.do_recv

syscall_call:
	mov	rax, [rbp + gseg.process]
	; saved rax: message code
	; rdi: target process
	; remaining: message params
	save_regs rdi,rsi,rdx,r8,r9,r10

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
	and	edx, PROC_IN_RECV | PROC_IN_SEND
	; If it's both IN_SEND and IN_RECV, that means we should treat it as
	; IN_SEND first (i.e. block)
	cmp	edx, byte PROC_IN_RECV
	jne	.block_on_send

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
; rsi: source process
; Note: we return from transfer_message if not blocked
transfer_message:
	push	rdi
	push	rsi

	; [rsp] = source/sender process
	; [rsp+8] = target/recipient process

	; Find the source handle we were sending from
	mov	rbx, [rsi + proc.rdi]
	; rcx = source handle object (assumed non-zero, because otherwise
	; there'd be no way for them to send anything!)

	mov	rdx, [rdi + proc.rdi] ; recipient handle
	test	rdx, rdx
	jz	.null_recipient_id
	mov	rax, [rdx + handle.other]
	test	rax, rax
	jz	.fresh_handle

	; not-fresh handle: check that the recipient did want a message from
	; *us*. If not, give an error back to sender and keep recipient waiting.
	cmp	rbx, rax
	push	qword [rdx + handle.key]
	pop	qword [rdi + proc.rdi]
	je	.rcpt_rdi_set

	; rbx = actual sender's handle
	; rdx = recipient's handle
	; rax = recipient's handle's other handle
%if log_messages
lodstr	rdi, 'rcpt handle %p -> handle %p != sender handle %p', 10
	mov	rsi, rdx
	mov	rdx, rax
	mov	rcx, rbx
	call	printf
%endif

	; Mismatched sender/recipient.
	PANIC

.fresh_handle
	; Get the source handle's other handle, i.e. the recipient handle.
	mov	rax, [rbx + handle.other]
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
	mov	rcx, rbx
	call	assoc_handles
	pop	rsi
	pop	rdi

	jmp	.null_recipient_id

.junk_fresh_handle:
	; rax = actual handle
	push	rdi
	push	rsi
	; rdx = recipient handle object we would've used
	mov	rdi, rdx
	call	free_frame

	pop	rsi
	pop	rdi

.null_recipient_id:
	mov	rax, [rbx + handle.other]
	test	rax, rax
	jz	.rax_has_key
.rax_has_handle:
	; rax is the recipient-side handle - pick out its key directly
	mov	rax, [rax + handle.key]
.rax_has_key:
	mov	[rdi + proc.rdi], rax
.rcpt_rdi_set:

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
lodstr	rdi, 'Copied message from %p to %p', 10
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
	test	[rsi + proc.flags], byte PROC_RUNNING
	mov	qword [rsi + proc.rdi], 0
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
%if need_print_procstate
	call	print_proc
%else
	mov	rdx, [rsi + proc.flags]
	call	printf
%endif
	pop	rdi
%endif

	tcall	block_and_switch

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
%assign idt_nvec 32
%rep 16
%if builtin_keyboard && idt_nvec == 33
	reg_vec 33, key_handler
%else
	reg_vec idt_nvec, handle_irq_ %+ idt_nvec
%endif
%endrep
%if builtin_timer
	reg_vec APIC_TIMER_IRQ, timer_handler
%endif
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
idt_end

section.bss.end:

section .rodata
section.data.end:

