; vim:filetype=nasm:

%include "common.inc"

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

bits 32
start32_mboot:
	; The multiboot loader will have set cs and ds to sensible segments,
	; but we have no guarantees that the IDT and GDT tables still exist
	; or what they contain.
	; We don't need IDT until we enable interrupts (after long mode)
	; Trick: use o16 to read a 24-bit offset instead of a 32-bit one
	; The high byte is 0xc0 because the 32-bit offset at idtr is in fact
	; just the lower 32-bit part of a 64-bit offset 0xffffffffc000XXXX.
	o16 lgdt	[gdtr]

	cmp	eax, 0x2BADB002
	; TODO Put some error message on the screen at least
	jne	$

	test	byte [ebx + mbootinfo.flags], MBI_FLAG_MMAP
	jz	$ ; Infinite loop fail! :)

find_start_of_memory:
	; Start of memory. Starts at 1MB, gets poked for any multiboot data we
	; see stored above 1MB. Note: assumes everything is stored *starting*
	; at 1MB, will break down if grub e.g. loads the modules at the *end*
	; of ram instead...
	mov	ebp, free_mem_start

	; Some notes:
	; kvm seems to put the modules and multiboot info directly after the
	; end of the bss data. Our pages struct (outside BSS) normally
	; prevents that from working.
	;
	; grub seems to load modules as early as possible after 0x100000.
	; If the kernel is in low memory, the multiboot info is in low memory
	; but the modules are still at 1MB.
	; If the kernel is loaded above 1MB, the multiboot info may still be
	; below 1MB.

find_mod_ends:
	mov	esi, [ebx + mbootinfo.mods_addr]
	mov	ecx, [ebx + mbootinfo.mods_count]
.mods_loop:
	jecxz	.mods_loop_out

	mov	eax, [esi + mboot_mod.end]
	cmp	eax, ebp
	jb	.below
	mov	ebp, eax
.below:
	add	esi, mboot_mod_size
	loop	.mods_loop
.mods_loop_out:

	; Align start-of-memory to page boundary (just to keep out of the
	; modules)
	add	ebp, 0xfff
	and	bp, 0xf000

copy_multiboot_info:
	mov	[mbi_pointer], ebp
	;mov	[orig_mbi_pointer], ebx

	mov	esi, ebx
	mov	edi, ebp
	mov	ecx, mbootinfo_size / 4
	rep movsd

	; TODO Should check for MBI_FLAG_MMAP
	; Copy the memory mappings
	mov	esi, [ebx + mbootinfo.mmap_addr]
	mov	[ebp + mbootinfo.mmap_addr], edi
	mov	ecx, [ebx + mbootinfo.mmap_length]
	rep movsb

%ifdef mboot_use_cmdline
	test	byte [ebx + mbootinfo.flags], MBI_FLAG_CMDLINE
	jz	.no_cmdline

	mov	esi, [ebx + mbootinfo.cmdline]
	mov	[ebp + mbootinfo.cmdline], edi
.strcpy_loop:
	lodsb
	stosb
	test	al,al
	jnz	.strcpy_loop

.no_cmdline:
%endif
copy_modules:
	; If we have modules, copy them too
	test	byte [ebx + mbootinfo.flags], MBI_FLAG_MODULES
	jz	.no_modules

	mov	esi, [ebx + mbootinfo.mods_addr]
	mov	[ebp + mbootinfo.mods_addr], edi
	mov	ecx, [ebx + mbootinfo.mods_count]
	jecxz	.no_modules
	lea	ecx, [ecx * (mboot_mod_size / 4)]
	rep movsd

	mov	esp, [ebx + mbootinfo.mods_addr]
	mov	edx, [ebp + mbootinfo.mods_addr]
	mov	ecx, [ebx + mbootinfo.mods_count]
.loop:
	mov	esi, [esp + mboot_mod.string]
	mov	[edx + mboot_mod.string], edi
.cp_str:
	lodsb
	test	al,al
	stosb
	jnz	.cp_str
	add	esp, mboot_mod_size
	add	edx, mboot_mod_size
	loop	.loop
.no_modules:

	; Memory start is just after the copied data
	add	edi, 0xfff
	and	di, 0xf000
	mov	[memory_start], edi

start32:

	; Remap PICs to 0x20..0x2f. We do this despite the fact we're going to
	; disable them just afterwards, since spurious interrupts may still
	; happen (and in any case, we want to be able to tell exceptions from
	; IRQs).
	mov	al, ICW1_INIT | ICW1_ICW4
	out	PIC1_CMD, al
	out	PIC2_CMD, al
	mov	al, 0x20
	out	PIC1_DATA, al
	mov	al, 0x28
	out	PIC2_DATA, al
	mov	al, ICW3_MASTER
	out	PIC1_DATA, al
	mov	al, ICW3_SLAVE
	out	PIC2_DATA, al

	mov	al, ICW4_8086
	out	PIC1_DATA, al
	out	PIC2_DATA, al

	; Disable PICs. Multiboot doesn't guarantee anything about their state.
	mov	al,0xff
	out	PIC1_DATA, al
	out	PIC2_DATA, al

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
	; (Oh, and all these are wrong since the move to >1MB)

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
	mov	edi, pages.pml4
	mov	eax, pages.low_pdp | 7 ; 0xb000 is where the PDP starts
	stosd

	zero	eax
	mov	ecx, 0x03ff ; number of zero double-words in PML4
	rep stosd

	; In 0x11000 we have another PDP. It's global and *not* user-accessible.
	mov	dword [edi-8], pages.kernel_pdp | 3

	; edi fallthrough, now == 0xb000
	; Write PDP (one entry, pointing to one PD)
	mov	eax, pages.low_pd | 7 ; 0xc000 is the start of the PD
	stosd

	xor	eax,eax
	mov	ecx, 0x03ff ; number of zero double-words in PDP
	rep stosd

	; Write PD (one entry, pointing to one PT)
	mov	eax, pages.low_pt | 7
	stosd
	xor	eax,eax
	mov	ecx, 0x03ff
	rep stosd

	; Write PT at 0xd000, kernel will be mapped at 0x100000 and up.
	xor	eax,eax
	mov	ecx,0x0400
	rep stosd
	sub	edi,0x1000-(pages.kernel / 0x1000) * 8
	; Map page 0x100000 and up, for bootstrapping. The booted kernel will
	; unmap these and keep only the higher-half stuff when it's ready.
	mov	ax, pages.kernel | 1
	mov	cl, kernel_pages
.map_kernel_pages:
	stosd
	add	di, 4
	add	ax, 0x1000
	loop	.map_kernel_pages

	; Page mapping for kernel space (top 4TB part)
	mov	edi, pages.kernel_pdp
	xor	eax,eax
	mov	ecx,0x0400
	rep	stosd
%if use_1gb_pages
	; Write a single 1GB page mapping at physical address 0
	mov	word [edi - 8],0x83
%else
	; Pointer to PD for upper 1GB of memory
	mov	dword [edi - 8], pages.kernel_pd | 3
.fill_pd:
	mov	edi, pages.kernel_pd
	mov	eax, 0x83
	; Fill in with 512 x 2MB blocks
	mov	ecx, 512
.loop:
	stosd
	add	edi, 4
	add	eax, 1 << 21
	loop	.loop
%endif

	; Start mode-switching
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
	mov	rsp, phys_vaddr(kernel_stack_end)
	; Start by jumping into the kernel memory area at -1GB. Since that's a
	; 64-bit address, we must do it in long mode...
	jmp	start64

section .bss
mbi_pointer resd 1
memory_start resd 1
;orig_mbi_pointer resd 1

section .text
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

section .rodata
align	4
gdtr:
	dw	gdt_end-gdt_start-1 ; Limit
	dq	text_vpaddr(gdt_start)  ; Offset

