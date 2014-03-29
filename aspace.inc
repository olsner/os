; vim:filetype=nasm:

; Ordinary and boring flags
MAPFLAG_X	equ 1
MAPFLAG_W	equ 2
MAPFLAG_R	equ 4
MAPFLAG_RWX	equ 7 ; All/any of the R/W/X flags

; Anonymous page: allocate frame on first use
MAPFLAG_ANON	equ 8
; Backdoor flag for physical memory mapping. handle is 0, .offset is (paddr -
; vaddr).
MAPFLAG_PHYS	equ 16
; Mix: automatically allocated page that is "locked" (really it only differs in
; that it's allocated at map time and the physical address is returned to the
; user).
MAPFLAG_DMA	equ (MAPFLAG_PHYS | MAPFLAG_ANON)
MAPFLAG_USER_ALLOWED equ MAPFLAG_PHYS | (MAPFLAG_PHYS - 1)

; mapcard: the handle, offset and flags for the range of virtual addresses until
; the next card.
; 5 words:
; - 3 for dict_node w/ vaddr
; - 1 handle
; - 1 offset+flags
;   (since offsets must be page aligned we have 12 left-over bits)
; This structure is completely unrelated to the physical pages backing virtual
; memory - it represents each process' wishful thinking about how their memory
; should look. backings and sharings control physical memory.
struc mapcard
	.as_node restruc dict_node
	.vaddr	equ .as_node
	.handle	resq 1
	; .vaddr + .offset = handle-offset to be sent to backer on fault
	; For a direct physical mapping, paddr = .vaddr + .offset
	; .offset = handle-offset - vaddr
	; .offset = paddr - .vaddr
	.offset	resq 1
	.flags	equ .offset ; low byte (12 bits?) of offset is flags
endstruc

; backing: mapping *one page* to the place that page came from.
; Indexed by vaddr for the process that maps it. The vaddr includes flags, so
; look up by vaddr|0xfff.
; This is likely to exist once per physical page per process. Should be
; minimized.
; 6 words:
; - 3 words for dict_node w/ vaddr
; - 1 word for parent
; - 2 words for child-list links
;
; Could be reduced to 4 words: flags, parent, child-list links, if moving to
; an external dictionary. 32 bytes per page gives 128 entries in the last-level
; table. Flags could indicate how many levels that are required, and e.g. a
; very small process could have only one level, and map 128 pages at 0..512kB.
struc backing
	.as_node restruc dict_node
	.vaddr	equ .as_node
	; Flags stored in low bits of vaddr!
	.flags	equ .vaddr
	; Pointer to parent sharing. Needed to unlink self when unmapping.
	; Could have room for flags (e.g. to let it be a paddr when we don't
	; need the parent - we might have a direct physical address mapping)
	.parent	resq 1
	; Space to participate in parent's list of remappings.
	.child_node restruc dlist_node
endstruc

; sharing: mapping one page to every place it's been shared to
; 7 words!
struc sharing
	.as_node restruc dict_node
	.vaddr	equ .as_node
	.paddr	resq 1
	.aspace	resq 1
	.children restruc dlist
endstruc

struc aspace
	; Upon setup, pml4 is set to a freshly allocated frame that is empty
	; except for the mapping to the kernel memory area (which, as long as
	; it's less than 4TB is only a single entry in the PML4).
	.pml4		resq 1
	; TODO Lock structure for multiprocessing
	.count		resq 1
	; Do we need a list of processes that share an address space?
	; (That would remove the need for .count, I think.)
	;.procs	resq 1
	.handles	restruc dict
	.pending	restruc dict

	.mapcards	restruc dict
	.backings	restruc dict
	.sharings	restruc dict
endstruc

