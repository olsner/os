%define probesection .stats
%macro DEFPROBE 1
[section probesection]
%xdefine _PROBE .probe_ %+ %1
_PROBE: resd 1
__SECT__
%endmacro

%macro PROBE 1
%if probes
DEFPROBE %1
	inc dword [_PROBE]
%else
; Nothing
%endif
%endmacro
