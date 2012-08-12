; vim:filetype=nasm:

MBOOT_MAGIC	equ	0x1BADB002

; flags 0..15: mandatory flags, we won't boot unless the loader supports them
%define MBOOT_FLAG_PAGE_ALIGNED (1 << 0)
%define MBOOT_FLAG_NEED_MEMMAP (1 << 1)
%define MBOOT_FLAG_NEED_VIDMODE (1 << 2)
; flags 16..: optional flags, the loader is allowed to ignore these
%define MBOOT_FLAG_LOADINFO (1 << 16)

%macro assert 1
%if (%1) == 0
%error assert %1 failed
%endif
%endmacro

%macro mboot 1
%push mboot_header
%assign mboot_flags %1
align 4
%%header_addr:
dd	MBOOT_MAGIC
dd	%1
dd	-(%1) - MBOOT_MAGIC
%assign header_pos 12
%endmacro

%macro mboot_dd 1-*
%assign header_pos (header_pos + (%0) * 4)
%rep %0
dd %1
%rotate 1
%endrep
%endmacro

; Provide load address information.
; Takes five arguments:
; - header address
; - load address
; - load_end_address
; - bss_end_address
; - entry_address
; TODO We should be able to figure out the appropriate address of the header
; ourselves!
%macro mboot_load 5
assert (mboot_flags & MBOOT_FLAG_LOADINFO)
assert (header_pos == 12)
mboot_dd %1, %2, %3, %4, %5
%endmacro

%macro endmboot 0
%if mboot_flags & (MBOOT_FLAG_LOADINFO)
assert header_pos >= 32
%endif
%pop
%endmacro

%define MBI_FLAG_CMDLINE (1 << 2)
%define MBI_FLAG_MODULES (1 << 3)
%define MBI_FLAG_MMAP (1 << 6)

; Data of .vbe member of mboot info
struc mboot_vbe
.control_info	resd 1
.mode_info	resd 1
.mode		resw 1
.iface_seg	resw 1
.iface_off	resw 1
.iface_len	resw 1
endstruc

struc mbootinfo
.flags resd 1
; if flags[0]:
.mem_lower	resd 1
.mem_upper	resd 1
; if flags[1]:
.boot_devices	resd 1

; if (flags & MBI_FLAG_CMDLINE)
.cmdline	resd 1

; if (flags & MBI_FLAG_MODULES)
.mods_count	resd 1
.mods_addr	resd 1

.syms		resd 4

; if (flags & MBI_FLAG_MMAP)
.mmap_length	resd 1
.mmap_addr	resd 1

.drives_length	resd 1
.drives_addr	resd 1

.config_table	resd 1

.boot_loader	resd 1
.apm_table	resd 1

.vbe		resb mboot_vbe_size
endstruc

struc mboot_mod
.start		resd 1
.end		resd 1
.string		resd 1
		resd 1 ; reserved
endstruc
