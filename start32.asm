global gdtr
extern start64
extern section.data.end
global start32_mboot

global mbi_pointer
global memory_start

section..text.vstart equ pages.kernel

%include "sections.inc"
%include "start32.inc"
