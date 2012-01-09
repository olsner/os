; vim:ts=8:sts=8:sw=8:filetype=nasm:
%assign strcounter 0
%define strsection .data
%macro STR 1+
[section strsection]
%xdefine _STR .str_ %+ strcounter
%assign strcounter strcounter+1
_STR: db %1, 0
__SECT__
%endmacro
%macro lodstr 2+
STR %2
	lea %1, [_STR]
%endmacro