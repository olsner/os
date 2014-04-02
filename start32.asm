global gdtr
extern start64
extern section.data.end

section..text.vstart equ pages.kernel

%include "sections.inc"
%include "start32.inc"
