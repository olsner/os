; US layout, scan code set 2/1 -> ASCII table
; Preprocessing: e0, e1, e2 are either ignored or add some constant to the key
; code value
; positive values are ascii key codes, negative values are some kind of special
; key. zeroes are ignored.

SPEC_SHIFT	equ -1
SPEC_CTRL	equ 0 ; ignored for now

keymap:
%if use_set2
%error Meh
; 01..0c: F keys
; 0d: tab
; 0e: `~
; 11: left Alt
; 12: left Shift
; 14: left control
; 15: q
%else
; 0: dummy value
db	0
; 1..e: first row
;
; 1: esc
; 2..b: 1..9, 0
; c: -_
; d: =+
; e: backspace = 8 (BS) - but could also be 0x74 (DEL)
db	27,'1234567890-=',8
; f..1c: second row, tab, qweryuiop[], ending with enter
db	9,'qwertyuiop[]',10
; 1d..28: left control, asdfghkl;',
db	SPEC_CTRL,"asdfghjkl;'"
; 29: `
db	'`'
; 2a..36: left shift, \zxcvbnm,./, right shift
db	SPEC_SHIFT,'\zxcvbnm,./', SPEC_SHIFT
; 37, 38, 39: keypad *, left alt, space
db	'*', 0, ' '
; 3a: CapsLock
; 2b..44: F1..F10
; 46,46: NumLock, ScrollLock
; 47..53: keypad: 789-456+1230.
; 57, 58: F11, F12
%endif

.end:
.numkeys equ .end - keymap
.shifted_:
.shifted equ .shifted_ - 1
;	27,'1234567890-=',8
db	27,'!@#$%^&*()_+',8
;	9,'qwertyuiop[]',10
db	9,'QWERTYUIOP{}',10
;	SPEC_CTRL,"asdfghjkl;'"
db	SPEC_CTRL,'ASDFGHJKL:"'
;	'`'
db	'~'
;	SPEC_SHIFT,'\zxcvbnm,./', SPEC_SHIFT
db	SPEC_SHIFT,'|ZXCVBNM<>?', SPEC_SHIFT
db	'*', 0, ' '
