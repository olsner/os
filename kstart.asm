; vim:ts=8:sts=8:sw=8:filetype=nasm:
; This is the real bootstrap of the kernel, and it is this part that is loaded
; by the boot sector (boot.asm)

[map all kstart.map]

; Configuration
%define log_switch_to 0
%define log_switch_next 0
%define log_runqueue 0
%define log_fpu_switch 0 ; Note: may clobber user registers if activated :)
%define log_timer_interrupt 0
%define log_page_fault 1
%define log_mappings 0
%define log_lookup_handle 0
%define log_waiters 0
%define log_messages 0

%define debug_tcalls 0

%assign need_print_procstate (log_switch_to | log_switch_next | log_runqueue)

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
	xor	%1, %1
%endmacro

%macro restruc 1-2 1
	resb (%1 %+ _size) * %2
%endmacro

%define TODO TODO_ __LINE__
%macro TODO_ 1-2 'TODO'
lodstr	rdi, %2, ' @ %x', 10 ; Decimal output would be nice...
	mov	esi, %1
	call	printf

	cli
	hlt
%endmacro

%define PANIC PANIC_ __LINE__
%macro PANIC_ 1-2 'PANIC'
lodstr	rdi, %2, ' @ %x', 10 ; Decimal output would be nice...
	mov	esi, %1
	call	printf

	cli
	hlt
%endmacro

%macro tcall 1
%if debug_tcalls
	call	%1
	ret
%else
	jmp	%1
%endif
%endmacro

%macro tc 2
%if debug_tcalls
	j%-1	%%skip
	tcall	%2
%%skip:
%else
	j%+1	%2
%endif
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
%include "mboot.inc"
%include "pages.inc"

RFLAGS_IF_BIT	equ	9
RFLAGS_IF	equ	(1 << RFLAGS_IF_BIT)
RFLAGS_VM	equ	(1 << 17)

APIC_TICKS	equ	10000
APIC_PBASE	equ	0xfee00000

; SYSCALLS!
;
; arguments: rdi, rsi, rdx, (not rcx! rcx stores rip), r8, r9, r10 (used instead
; of rcx in syscalls)
; syscall-clobbered: rcx = rip, r11 = flags
; syscall-preserved: rbp, rbx, r12-r15, rsp
; return value(s): rax, rdx
;
; Message-passing through registers uses rax and rdi to send and return the
; message code and sender/recipient. rsi, rdx, r8-10 are message parameters.

SYSCALL_WRITE	equ	0
SYSCALL_GETTIME	equ	1
SYSCALL_YIELD	equ	2

; Create a new process. Is this similar to fork? Details not really determined.
;
; The new process will have an empty address space, making it crash as soon as
; it can.
;
; rdi: entry-point for new process
; rsi: rsp for new process
; Returns:
; rax: process handle for child process or 0 on error (but what about error
; codes?)
; The entry point will have
; rdi: process handle for parent process
; rsi: ???
; rax: 0
; (remaining registers could be parameters for the process entry point, copied
; from the calling process)
SYSCALL_NEWPROC	equ	3

; Send a message, wait synchronously for response from the same process.
; Works the same as SYSCALL_SEND followed by SYSCALL_RECV but more efficient,
;
; Should add some way to let the receive part be a receive-from-any. A server
; can get rid of a lot of syscalls by allowing it to send the response to proc
; A at the same time as it receives the next request from any process.
; Otherwise it'd need to SEND then return back just so that it can do a new
; receive-from-any RECV.
;
; Takes:
; rdi: message code
; rsi: send-to (and receive-from) process handle.
; <message parameters>
;
; (See also SYSCALL_SEND)
;
; Returns:
; If the send was successful: see SYSCALL_RECV.
; Otherwise, you'll get an error response in rax, and you'll probably not know
; if it was the SEND or the RECV that failed. (TBD: would such information be
; reliably useful?)
SYSCALL_SENDRCV	equ	4

; Receive a message
;
; rdi: receive-from (0 = receive-from-any)
;
; Returns in registers:
; rax: message code
; rdi: sending process ID
; Remaining argument registers: message data
SYSCALL_RECV	equ	5

; Send a message
; rax: message code
; rdi: target process ID
; Remaining argument registers: message data
; Returns:
; rax: error code or 0
SYSCALL_SEND	equ	6

SYSCALL_HALT	equ	7

; Send a message without waiting for response, will fail instead of block if
; the target process is not in blocking receive.
SYSCALL_ASEND	equ	7

; Receive a message if there is already some process IN_SEND waiting for us to
; become IN_RECV, fail immediately otherwise.
SYSCALL_ARECV	equ	8

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



kernel_base equ -(1 << 30)
handle_top equ 0x7fff_ffff_f000 ; For now, we waste one whole page per handle (I think)

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

	.free_frame	resq 1
	.temp_xmm0	resq 2
endstruc

section .text vstart=pages.kernel
section .data vfollows=.text follows=.text align=4
section usermode vfollows=.data follows=.data align=1
section bss nobits align=8 vfollows=usermode
section memory_map nobits vstart=pages.memory_map

; get the physical address of a symbol in the .text section
%define text_paddr(sym) (section..text.vstart + sym - text_vstart_dummy)
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
text_vstart_dummy:

%include "start16.inc"
%include "start32.inc"

align 4
mboot_header:
mboot MBOOT_FLAG_LOADINFO | MBOOT_FLAG_NEED_MEMMAP
mboot_load \
	text_paddr(mboot_header), \
	section..text.vstart, \
	section.usermode.end, \
	section.memory_map.end, \
	text_paddr(start32_mboot)
endmboot

bits 64
default rel
;;
; The main 64-bit entry point. At this point, we have set up a page table that
; identity-maps 0x8000 and 0x9000 and maps all physical memory at kernel_base.
;;
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

	mov	ax,data64_seg
	mov	ds,ax
	mov	ss,ax
	mov	fs,ax
	mov	gs,ax

	lea	rdi,[rel section.bss.vstart]
	lea	rcx,[rel section.bss.end]
	sub	rcx,rdi
	shr	ecx,2
	zero	eax
	rep stosd

	mov	rdi,phys_vaddr(0xb8004)
	lea	rsi,[rel message]
	mov	cl, 4
	rep movsd

	mov	rsp, phys_vaddr(kernel_stack_end)
	mov	ax,tss64_seg
	ltr	ax

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

	mov	rbp,kernel_base-0x1000+rbpoffset
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

	mov	r11, phys_vaddr(0xb8020)
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
lodstr	rdi,	'%x frames allocated', 10
	mov	rsi, rbx
	call	printf

.free_loop:
	mov	rdi, r12
	test	rdi, rdi
	jz	launch_user
	mov	r12, [rdi]
	call	free_frame
	jmp	.free_loop

; Set up a new process with some "sane" defaults for testing, and add it to the
; run queue.
;
; rdi: user_entry
new_user_proc:
	mov	esi, user_stack_end
	call	new_proc
	push	rbx
	mov	rbx, rax ; save away new process in callee-save register
	mov	rdi, rax
	call	runqueue_append

	; Replicate the hard-coded read-only region for 0x8000 and 0x9000
	call	allocate_frame ; TODO allocate region, not whole page :)
	mov	edx, pages.kernel
	mov	ecx, 0x2000
	mov	[rax + region.paddr], edx
	mov	[rax + region.size], ecx
	mov	rdi, [rbx + proc.aspace]
	mov	rsi, rax
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_X

user_stack_end	equ	0x13000

	mov	rdi, [rbx + proc.aspace]
	zero	esi
	; Map the user stack as an allocate-on-use ("anon") page
	mov	rdx, user_stack_end - 0x1000
	mov	rcx, 0x1000
	call	map_region
	mov	byte [rax + mapping.flags], MAPFLAG_R | MAPFLAG_W | MAPFLAG_ANON

	mov	rax, rbx
	pop	rbx
	ret

launch_user:
	call	allocate_frame
	o64 fxsave [rax]
	mov	[rel globals.initial_fpstate], rax

	; Make the first use of fpu/multimedia instructions cause an exception
	mov	rax,cr0
	bts	rax,CR0_TS_BIT
	mov	cr0,rax

	mov	edi, user_entry
	call	new_user_proc
	mov	rbx, rax ; Save for later
	mov	edi, user_entry_2
;call	new_user_proc
	mov	edi, user_entry_3
;call	new_user_proc

	call	switch_next

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
	PANIC
.already_running:
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
lodstr	rdi, 'idle', 10
	call	printf
	hlt

switch_next:
%if log_switch_next
	mov	r12, rax
	mov	r13, rdx
lodstr	rdi, 'switch_next', 10
	call	puts
	call	print_procstate
	mov	rax, r12
	mov	rdx, r13
%endif

	call	runqueue_pop
	test	rax, rax
	jz	idle
	tcall	switch_to

%ifdef need_print_procstate
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
	call	puts
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
	TODO
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
	load_regs rbp,rbx,r12,r13,r14,r15
.fast_fastret:
	mov	rsp, [rax+proc.rsp]
	mov	rcx, [rax+proc.rip]
	mov	r11, [rax+proc.rflags]
	mov	rax, [rax+proc.rax]
	o64 sysret

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
	jnz	dlist_remove
	ret
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

; rdi: the address space being mapped into
; rsi: the kernel object being mapped
; For starters, all handles point to processes and have no flags.
map_handle:
	test	rsi, 0xf
	jnz	.unaligned_kernel_object

	; Calculate the virtual address for the handle
	mov	rdx, [rdi + aspace.handles_bottom]
	sub	rdx, 0x1000
	mov	[rdi + aspace.handles_bottom], rdx

	; rdi: address space
	zero	ecx ; size is 0
	push	rdx ; vaddr of handle, also return value
	push	rsi
	zero	rsi ; rsi: region (== 0, we'll set it later)
	call	map_region
	; pop the kernel object pointer (the rsi we pushed above).
	; MAPFLAG_HANDLE indicates the region pointer is not a region but a
	; kernel object.
	pop	qword [rax + mapping.kobj]
	mov	byte [rax + mapping.flags + 2], MAPFLAG_HANDLE >> 16
	pop	rax
	ret

.unaligned_kernel_object:
lodstr	rdi,	'Unaligned kobj %p', 10
	call	printf
	cli
	hlt

; rdi: address space
; rsi: handle to look up
lookup_handle:
%if log_lookup_handle
	push	rsi
%endif
	mov	rdi, [rdi + aspace.mappings + dlist.head]
.loop:
%if log_lookup_handle
	push	rdi
	push	rsi
	mov	rsi, [rdi + mapping.vaddr - mapping.as_node]
	mov	rdx, [rdi + mapping.kobj - mapping.as_node]
lodstr	rdi, 'lookup_handle: vaddr=%p kobj=%p', 10
	call	printf
	pop	rsi
	pop	rdi
%endif

	test	rdi, rdi
	jz	.not_found
	mov	rax, [rdi + mapping.vaddr - mapping.as_node]
	cmp	rax, rsi
	je	.found
	mov	rdi, [rdi + dlist_node.next]
	jmp	.loop

.found:
	mov	rax, [rdi + mapping.kobj - mapping.as_node]
	test	byte [rdi + mapping.flags - mapping.as_node + 2], MAPFLAG_HANDLE >> 16
	jz	.not_found

%if log_lookup_handle
lodstr	rdi,	'lookup_handle on %p: %p', 10
	pop	rsi
	mov	rdx, rax ; looked-up target process
	push	rax
	call	printf
	pop	rax
%endif

	ret

.not_found:
%if log_lookup_handle
lodstr	rdi, 'lookup_handle %p: not found.', 10
	pop	rsi ; handle to look up
	call	printf
%endif
	zero	eax
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

timer_handler:
	push	rax
	push	rbp
	; FIXME If we got here by interrupting kernel code, don't swapgs
	; But currently, all kernel code is cli, so we can only get here as the
	; result of a fault...
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
	mov	rdi, [rdi + aspace.mappings + dlist.head]
	mov	rsi, cr2
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
%if log_mappings
lodstr	rdi, 'Map %p: %p sz %x (%p<-->%p)', 10
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
	pop	rbx ; We don't need to save rax in the process, it's clobbered.
	pop	qword [rax + proc.rip]

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
	sc write
	sc gettime
	sc yield
	sc newproc
	sc sendrcv
	sc recv
	sc send
	sc halt
.end_table
N_SYSCALLS	equ (.end_table - .table) / 4

syscall_write:
	movzx	edi, dil
	or	di, 0xf00
	jmp	kputchar

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

syscall_newproc:
	; TODO: Validate some stuff
	; rdi: entry point
	call	new_user_proc
	mov	rdi,rax
	mov	rbx,rax ; Save new process id
	call	runqueue_append

	; FIXME How should we decide which address space to use for the new
	; process? For now we use new_user_proc, which will set up defaults.
	;lea	rax, [phys_vaddr(aspace_test)]
	;mov	[rbx + proc.aspace], rax
	;mov	rsi, [rbp + proc.aspace]
	;call	add_process_to_aspace
	;call	rdi, rbx

	mov	r12, [rbp + gseg.process]
	mov	rdi, [r12 + proc.aspace]
	mov	rsi, rbx ; New process that should be mapped
	call	map_handle
	; r13: handle to new process
	mov	r13, rax

lodstr	rdi, 'newproc %p at %p', 10
	mov	rsi, rbx
	mov	rdx, rax
	call	printf

	mov	rdi, [rbx + proc.aspace]
	mov	rsi, [rbp + gseg.process]
	call	map_handle
	; New process' first argument: creator pid
	mov	[rbx + proc.rdi], rax

	mov	rax, r13
	ret

syscall_halt:
	lodstr	rdi, '<<HALT>>'
	call	printf
	cli
	hlt

syscall_send:
	mov	rax, [rbp + gseg.process]
	save_regs rdi,rsi,rdx,r8,r9,r10

	mov	rdi, [rax + proc.aspace]
	; rsi = target process in user address space
	call	lookup_handle
	; rax = target proc object

	mov	rsi, [rbp + gseg.process]
	or	[rsi + proc.flags], byte PROC_IN_SEND

	mov	rdi, rax ; target
	tcall	send_or_block

syscall_recv:
	save_regs rsi

	test	rsi, rsi
	jz	.no_source_given

	mov	rdi, [rbp + gseg.process]
	mov	rdi, [rdi + proc.aspace]
	call	lookup_handle
	mov	rsi, rax

.no_source_given:
	; rsi = source process (?)
	mov	rdi, [rbp + gseg.process]
	or	[rdi + proc.flags], byte PROC_IN_RECV

	tcall	recv_from

syscall_sendrcv:
	mov	rax, [rbp + gseg.process]
	; rdi: message code, rsi: target process, remaining: message params
	save_regs rdi,rsi,rdx,r8,r9,r10

	mov	rdi, [rax + proc.aspace]
	; rsi = target process
	mov	r12, rsi
	call	lookup_handle
	mov	r13, rax
	; TODO Check that the handle was correct and actually a process

	; 1. Set IN_SEND and IN_RECV for this process
	mov	rsi, [rbp + gseg.process]
	or	[rsi + proc.flags], byte PROC_IN_SEND | PROC_IN_RECV

	mov	rdi, r13
	tcall	send_or_block

; Returns unless it blocks.
; rdi: recipient
; rsi: source (the process that will block if it can't send)
; rsi must be IN_SEND or IN_SEND|IN_RECV
; rdi may be in any state.
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
	btr	dword [rsi + proc.flags], PROC_RUNNING_BIT
	; If the running bit was set on the process we just blocked, we must
	; context switch
	tc c,	switch_next
	; Otherwise, just tailcall add_to_waiters, return to caller.
	tcall	add_to_waiters

; rdi: target process
; rsi: source process
; Note: we return from transfer_message if not blocked
transfer_message:
%macro copy_regs 0-*
%rep %0
	mov	rax, [rsi + proc. %+ %1]
	mov	[rdi + proc. %+ %1], rax
	%rotate 1
%endrep
%endmacro
	; If message-parameter registers were ordered in the file, we could
	; do a rep movsd here.
	; rsi is not copied - that was just the sender's process handle to the
	; recipient process.
	copy_regs rdi, rdx, r8, r9, r10

	and	[rdi + proc.flags], byte ~PROC_IN_RECV
	push	rdi
	push	rsi
	mov	rdx, rdi
%if log_messages
lodstr	rdi, 'Copied message from %p to %p', 10
	call	printf
%endif

	mov	rdi, [rsp] ; source process, the one we were waiting for
	; The recipient is guaranteed to be unblocked by this, make it stop
	; waiting.
	mov	rsi, [rsp + 8] ; recipient, the one being unblocked
	test	[rsi + proc.flags], byte PROC_RUNNING
	jnz	.not_blocked
	call	stop_waiting
.not_blocked:

	; rdi: sending process, it's no longer sending
	pop	rdi
	pop	rsi

	; If the previously-sending process is now receiving (was in sendrcv),
	; continue with its receiving phase. Otherwise unblock it.
	and	[rdi + proc.flags], byte ~PROC_IN_SEND
	test	[rdi + proc.flags], byte PROC_IN_RECV
	tc nz, recv_from
	test	[rdi + proc.flags], byte PROC_RUNNING
	; rdi should stop waiting for rsi now, swap the registers
	xchg	rsi, rdi
	tc z,	stop_waiting
	ret

; rdi: process that might have become able to receive
; rsi: process it wants to receive from
; If this does not block, it will return to the caller.
recv_from:
	test	[rsi + proc.flags], byte PROC_IN_SEND
	tc nz,	transfer_message

%if log_messages
	push	rdi
	push	rsi
	mov	rdx, rsi
	mov	rsi, rdi
lodstr	rdi, '%p blocked on receive from %p', 10
	call	printf
	pop	rdi
	pop	rsi
%else
	xchg	rsi, rdi
%endif
	btr	dword [rsi + proc.flags], PROC_RUNNING_BIT
	tc nc,	add_to_waiters
	; Looks like we couldn't do anything more right now, so just do
	; something else for a while.
	tcall	switch_next

%if 0
; Find a sender that's waiting to send something to this process. If one
; exists, transfer the message.
;
; Receiving half:
; 1. Check waiter queue, find the process we're receiving from, or any
; process that's in IN_SEND and sending to us.
; 2. If we found a process:
; 2.1. Remove it from waiter queue, it's no longer waiting.
; 2.2. Do transfer_message to it
;
recv_from_any:
	; [rsp] = recipient, [rsp+8] = sender, if any
	mov	rax, [rdi + proc.waiters]
.loop:
%if 1
	push	rax
lodstr	rdi,	'recv: %p (%x)', 10
	lea	rsi, [rax - proc.node]
	test	rax, rax
	cmovz	rsi, rax
	call	print_proc
	pop	rax
%endif
	test	rax, rax
	jz	.no_senders
	lea	rsi, [rax - proc.node]
	test	[rax - proc.node + proc.flags], byte PROC_IN_SEND
	jnz	transfer_message

	mov	rax, [rax + dlist_node.next]
	jmp	.loop

.no_senders:
lodstr	rdi,	'No senders found to %p', 10
	mov	rsi, [rsp]
	call	printf

	jmp	switch_next
%endif

section usermode

user_entry_new2:
	push	rdi
	mov	rsi, rdi
	mov	eax, SYSCALL_RECV
	syscall
.loop:
	mov	edi, msg_req(MSG_USER)
	mov	rsi, [rsp]
	mov	eax, SYSCALL_SENDRCV
	syscall
	inc	r10
	jmp	.loop

user_entry_new:
	; Creator pid (not parent, parents don't really exist here...)
	push	rdi

	; TODO Define the initial program state for a process.
	; - parent pid
	; - function parameters in registers?
	; - address space

lodstr	rdi, 'user_entry_new', 10
	call	printf

.loop:
	mov	rsi, [rsp]
	mov	eax, SYSCALL_RECV
	syscall

	mov	rsi, rdi
	mov	rcx, r10
	push	rdx
	push	r8
	push	r9
	push	r10
lodstr	rdi, 'new received %p %x %x %x %x', 10
	call printf

	pop	r10
	pop	r9
	pop	r8
	pop	rdx
	mov	edi, msg_resp(MSG_USER)
	mov	rsi, [rsp]
	mov	eax, SYSCALL_SEND
	inc	edx
	syscall

	jmp	.loop

user_entry:
	mov	edi, user_entry_new
	mov	rsi, rsp
	mov	eax, SYSCALL_NEWPROC
	syscall

	mov	rbx, rax

lodstr	rdi, 'newproc: %p', 10
	mov	rsi, rax
	call	printf

	mov	rdx, 1
	mov	r10, 2
	mov	r8, 3
	mov	r9, 4

.recv_from_new:
	mov	rsi, rbx
	mov	eax, SYSCALL_SENDRCV
	mov	edi, msg_req(MSG_USER)
	syscall

	push	rdx
	push	r8
	push	r9
	push	r10
	mov	rsi, rdi
	mov	rcx, r10
lodstr	rdi, 'old received %p %x %x %x %x', 10
	call	printf
	pop	r10
	pop	r9
	pop	r8
	pop	rdx

	jmp	.recv_from_new

user_entry_old:
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
	lea	edi,['a'+rbx]
	xor	eax,eax
	syscall

	paddq	xmm0, xmm1
	dec	ebx
	jnz	.loop

.end:
lodstr	rdi,	'Hello World from puts', 10
	call	puts

lodstr	rdi,	'printf %% "%s" %c',10,0
lodstr	rsi,	'Hello World',0
	mov	edx,'C'
	call	printf

	; Delay loop
	mov	ecx, 100000
	loop	$

	jmp	.start

puts:
	; callee-save: rbp, rbx, r12-r15
	mov	rsi,rdi

.loop:
	lodsb
	mov	edi,eax
	test	dil,dil
	jz	.ret

	push	rsi
	call	putchar
	pop	rsi
	jmp	.loop

.ret:
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
	and	di, 0xff
	or	di, 0x1f00

; Note: Takes gseg in rbp
kputchar:
	mov	eax, edi
	mov	rdi, [rbp + gseg.vga_pos]
	out	0xe9, byte al
	cmp	al,10
	je	.newline

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
	mov	eax, 0
	mov	ecx, 160 / 8
	rep	movsq
	jmp	.ret

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
	mov	al,0
	rep	stosw

	jmp	.finish_write

printf:
	; al: number of vector arguments (won't be used...)
	; rdi: format string
	; rsi,rdx,rcx,r8,r9: consecutive arguments

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
	cmp	al,'x'
	je	.fmt_x
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

	lea	rsi, [rel null_str]
	mov	rdi, [rdi]
	test	rdi, rdi
	cmovz	rdi, rsi
	call	puts

	mov	rsi,r12
	mov	rdi,r13
	jmp	.nextchar

.fmt_p:
	cmp	qword [rdi], 0
	; Rely on the special-case for null strings to print (null)
	jz	.fmt_s
.fmt_x:
	lea	r13,[rdi+8]
	mov	r12,rsi
	mov	rbx, [rdi]

	cmp	al, 'x'
	setz	r15b

	mov	edi, '0'
	call	putchar
	mov	edi, 'x'
	call	putchar

	mov	cl, 64

	; cl = highest bit of first hex-digit to print (>= 4)
	; rbx = number to print
.print_digits:
	lea	r14, [rel digits]
.loop:
	sub	cl, 4
	mov	rdi, rbx
	shr	rdi, cl
	and	edi, 0xf
	jnz	.print
	test	cl, cl
	jz	.print
	test	r15b, r15b
	jnz	.next_digit
.print:
	mov	r15b, 0
	mov	dil, byte [r14 + rdi]
	push	rcx
	call	putchar
	pop	rcx
.next_digit:
	test	cl, cl
	jnz	.loop

	mov	rsi,r12
	mov	rdi,r13
	jmp	.nextchar

.done:
	pop	rbx
	pop	r13
	pop	r12
	clear_clobbered
	add	rsp,48
	jmp	[rsp-48]

section .data

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
digits:
	db '0123456789abcdef'
null_str:
	db '(null)', 0

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
.data:	resd	1023

section bss
align 8
section.bss.end:

section memory_map
section.memory_map.end:

section .data
section.data.end:

section usermode
section.usermode.end:

