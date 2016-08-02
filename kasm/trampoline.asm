; Needed for each new cpu (at least):
; * Space for GDT (maybe in gseg)
; * Kernel stack
; * gseg
;
; We can generally reuse the IDT (it has no per-cpu data), and the TSS. The
; busy flag is in the GDT which will be per-cpu.
smp_trampoline:

%macro dbg 1
	mov	al, %1
	out	0xe9, al
%endmacro

%define cs_base 0x100

bits 16
.start16:
	jmp .after_data
; Used to report status
.sema		dq 0
; Space for the real stack once we're in long mode (this might be a 64-bit
; address...)
.kernel_stack	dq 0
; Make a copy of the common GDT for the new CPU
.after_data:
	dbg '0'
	mov	ax, 0
	mov	ds, ax
	mov	ss, ax
	lock inc byte [.sema]
	mov	sp, 0x1000
	dbg '1'
	push	word 0 ; GDT offset (high)
	push	word 0 ; GDT offset (low)
	push	word 0 ; Segment for GDT
	lgdt [ss:0x1000 - 6]
	dbg '2'

	; Enable protected mode without paging (our paging structures are
	; 64-bit and not usable as-is from 32-bit modes)
	mov eax, cr0
	or eax, 1
	mov cr0, eax

	dbg '3'

	jmp far code_seg:.start32

bits 32
.start32:
	dbg 'P'
	mov	eax, CR4_PAE | CR4_MCE | CR4_PGE | CR4_PCE | CR4_OSFXSR | CR4_OSXMMEXCPT
	mov	cr4, eax

	mov	edx, pages.pml4 ; address of PML4
	mov	cr3, edx

	mov	ecx, MSR_EFER
	rdmsr
	or	eax, 0x100 ; Set LME
	wrmsr

	mov	eax,cr0
	or	eax, CR0_PG | CR0_MP ; Enable paging, monitor-coprocessor
	and	al, ~CR0_EM ; Disable Emulate Coprocessor
	mov	cr0,eax

	jmp	code64_seg:.trampoline

bits 64
.trampoline:
	dbg 'L'
	mov	rsp, [rel .kernel_stack]
	; Start by jumping into the kernel memory area at -1GB. Since that's a
	; 64-bit address, we must do it in long mode...
	jmp	text_vpaddr(start64_ap)
