; Needed for each new cpu (at least):
; * Space for GDT (maybe in gseg)
; * Kernel stack
; * gseg
;
; We can generally reuse the IDT (it has no per-cpu data), and the TSS. The
; busy flag is in the GDT which will be per-cpu.
smp_trampoline:

%macro dbg 1
%if 0
	mov	al, %1
	out	0xe9, al
	mov	al, 10
	out	0xe9, al
%endif
%endmacro

%define cs_base 0x100

bits 16
.start16:
	jmp .after_data
; Used to report status
.sema		dq 0
; Space for the real stack once we're in long mode
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
	push	dword text_vpaddr(gdt_start) ; GDT offset (low)
	push	word gdt_end-gdt_start-1 ; GDT limit
	lgdt	[0x1000 - 6]
	dbg '2'

	; Enable protected mode without paging (our paging structures are
	; 64-bit and not usable as-is from 32-bit modes)
	mov eax, cr0
	or eax, 1
	mov cr0, eax

	dbg '3'

	; Requires a GDT...
	jmp far code_seg:.start32

bits 32
.start32:
	dbg 'P'
	mov	eax, CR4_PAE | CR4_MCE | CR4_PGE | CR4_PCE | CR4_OSFXSR | CR4_OSXMMEXCPT
	mov	cr4, eax
	dbg '4'

	mov	edx, pages.pml4 ; address of PML4
	mov	cr3, edx

	dbg '5'
	mov	ecx, MSR_EFER
	rdmsr
	or	eax, 0x100 ; Set LME
	wrmsr

	dbg '6'
	mov	eax,cr0
	or	eax, CR0_PG | CR0_MP ; Enable paging, monitor-coprocessor
	and	al, ~CR0_EM ; Disable Emulate Coprocessor
	mov	cr0,eax

	dbg '7'
	jmp	code64_seg:.trampoline

bits 64
.trampoline:
	dbg 'L'

	mov	rsp, [rel .kernel_stack]
	push	byte 1
	; Start by jumping into the kernel memory area at -1GB. Since that's a
	; 64-bit address, we must do it in long mode...
	jmp	text_vpaddr(start64_ap)
