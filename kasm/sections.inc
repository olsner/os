; vim:filetype=nasm:

section .rodata
rodata_vstart_dummy:
section .bss
bss_vstart_dummy:
section .text
text_vstart_dummy:

; get the physical address of a symbol in the .text section
%define text_paddr(sym) (section..text.vstart + sym - text_vstart_dummy)
%define bss_paddr(sym) (section..bss.vstart + sym - bss_vstart_dummy)
%define rodata_paddr(sym) (section..rodata.vstart + sym - rodata_vstart_dummy)
; get the virtual (kernel) address for a symbol in the .text section
%define text_vpaddr(sym) phys_vaddr(text_paddr(sym))
; get the virtual (kernel) address for a .bss-section symbol
%define bss_vpaddr(sym) phys_vaddr(bss_paddr(sym))
%define rodata_vpaddr(sym) phys_vaddr(rodata_paddr(sym))
; translate a physical address to a virtual address in the 'core'
; Note: in most cases, RIP-relative addressing is enough (since we tell the
; assembler we're based at 0x8000 while we're actually at kernel_base+0x8000),
; but this can be used for constant data or wherever we need the full address.
%define phys_vaddr(phys) kernel_base + phys

; Get the .text-relative vaddr of another section's LMA
%define section_vaddr(sect) (text_vstart_dummy + (section. %+ sect %+ .start - section..text.start))
