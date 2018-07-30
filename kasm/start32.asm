global gdtr
extern start64
extern section.data.end
global start32_mboot
global start32.trampoline

global mbi_pointer
global memory_start
global kernel_pdp

global gdtr

section..text.vstart equ pages.kernel
kernel_pdp equ pages.kernel_pdp
%define kernel_pages 5

%define mboot_use_cmdline 1
%define use_1gb_pages 0

%include "sections.inc"
%include "start32.inc"
