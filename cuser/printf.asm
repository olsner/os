bits 64
section .text
global printf

%include "../macros.inc"
%include "../syscalls.inc"
%include "../messages.inc"

%macro clear_clobbered 0
; no-op
%endmacro

%include "../user/putchar.asm"
%include "../printf.asm"
