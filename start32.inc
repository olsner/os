; vim:ts=8:sts=8:sw=8:filetype=nasm:
bits 32
start32_mboot:
	; The multiboot loader will have set cs and ds to sensible segments,
	; but we have no guarantees that the IDT and GDT tables still exist
	; or what they contain.
	; Trick: use o16 to read a 24-bit offset instead of a 32-bit one
	; The high byte is 0xc0 because the 32-bit offset at idtr is in fact
	; just the lower 32-bit part of a 64-bit offset 0xffffffffc000XXXX.
	o16 lidt	[idtr]
	o16 lgdt	[gdtr]

	cmp	eax, 0x2BADB002
	jne	start32

	test	byte [ebx + mbootinfo.flags], MBI_FLAG_MMAP
	jz	$ ; Infinite loop fail! :)

	mov	ecx, [ebx + mbootinfo.mmap_length]
	mov	esi, [ebx + mbootinfo.mmap_addr]
	add	ecx, esi ; ecx == end of map
	mov	edi, memory_map.data

	xor	eax, eax
.loop:
	cmp	ecx, esi
	jb	.done

	; ebp := pointer to next entry
	lodsd
	mov	ebp, esi
	add	ebp, eax

	; Copy 16 bytes (base_addr + length)
	%rep 4
	movsd
	%endrep
	; Read and test memory type
	lodsd
	cmp	eax, E820_MEM
	je	.use
	; Unusable memory, set type to 0 before we copy it
	xor	eax, eax
.use:
	stosd
	mov	esi, ebp
	jmp	.loop

.done:
	sub	edi, memory_map.data
	mov	[memory_map.size], edi

start32:
	; Disable PIC. start16.inc does this, but multiboot does *not*
	; guarantee anything about the PIC state.
	mov	al,0xff
	out	0xa1, al
	out	0x21, al

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

	; Write PT at 0xd000, will have a few PTE's first that are not present
	; to catch null pointers. Then at 0x8000 to 0x10000 we'll map pages to
	; the same physical address.
	xor	eax,eax
	mov	ecx,0x0400
	rep stosd
	sub	edi,0x1000-(pages.kernel / 0x1000) * 8
	; Map page 0x8000 and 0x9000 to the same physical address
	; 0x5 means user-accessible (4) plus present (1)
	mov	eax, pages.kernel | 5
	stosd
	add	edi, 4
	mov	eax, (pages.kernel + 0x1000) | 5
	stosd

	; Page mapping for kernel space (top 4TB part)
	mov	edi, pages.kernel_pdp
	xor	eax,eax
	mov	ecx,0x0400
	rep	stosd
	; Write a single 1GB page mapping at physical address 0
	mov	word [edi-8],0x1c3

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

	jmp	code64_seg:start64
