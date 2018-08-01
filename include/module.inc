; vim:filetype=nasm:

%define in_module 1

; Takes one argument: entry point label
%macro defmod 1
jmp	%1
%endmacro

%include "macros.inc"
%include "messages.inc"
%include "string.inc"

bits 64
default rel

section .text vstart=0x100000
section .rodata vfollows=.text follows=.text
section module_end nobits vfollows=.rodata align=1
section .bss nobits align=8 vfollows=.rodata align=4096

section module_end
end_of_module:
section .text
module:
