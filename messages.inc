; vim:filetype=nasm:

; Tries to fit into the SysV syscall ABI for x86-64.
; Message registers: rsi, rdx, r8, r9, r10
; (rcx and r11 are used by syscall.)

; Placeholder for e.g. no message received
MSG_NONE	equ	0

; Map a page of this object (more like a syscall)
; * flags:
;   * read-only/read-write/read-exec
;   * shared or private/CoW
;   * arbitrary flags on receiving side? or official mapper/backer side flags?
;   * CoW = requesting RW locally but RO backer-side
;     not all backers necessarily support CoW though - needs a supply of frames
;     this will be notified by the PFAULT handler not succeeding
; * virtual address on receiving side
; * offset inside object
; * length to map
;   does not matter except for management of local address space - we'll
;   notice at fault time if there's no such page.
;
; Must be called with a CALL.
; The reply will look like MSG_MAP if successful.
MSG_MAP		equ	1

; Received when a fault happens in a process that has mapped a page from you.
; Can not be sent.
;
; Parameters:
; * handle (in local address space) of mapped object
; * offset inside object
; * requested local+remote access
;   * for a read access, this will always be RO+RO
;   * for an instruction fetch of an already paged-in frame, might say RX+RO
;   * for a write access with locally R/W and remote R/O access, perform the
;     necessary gyrations to copy the page for CoW (or fail)
; (* length in bytes - or always exactly one page)
;
; Reply with MSG_PFAULT where:
; * offset is replaced with the local vaddr of the unfaulted page
;   this page will be shared with the receiving process:
;   * if currently mapped to a physical page, the same frame is reused
;   * if mapped recursively to another object, that object will get to handle
;     faults that are within the returned flags
;   (but flags may still limit access to that memory)
; * flags are the actual access the process should receive
;   * all future access that doesn't violate these flags will either proceed
;     and actually access/modify memory, or pfault recursively to an underlying
;     virtual memory object
;   * any future access that violates these flags will generate a new pfault
;     to this process.
;   * it's an error to try to provide more access than this process has to the
;     underlying mapping
;     note: this is the virtual access, not what's currently in the active
;     page table. For example, you can hand out a R/W mapping to your own R/W
;     mapping even if that's currently backed by a R/O frame that has not yet
;     been CoW:ed.
;
; (Optimization: when all your access to a page is given to another process,
; maybe the underlying memory can be mapped directly and the intermediate
; object unmapped? That would allow some levels of indirection to be
; removed...)
;
; TODO Revoking pages?
MSG_PFAULT	equ	2

; Unmap a virtual address range
; * object handle to unmap memory from (0 = unmap from self)
; * vaddr base or offset to unmap
; * length to unmap (bytes)
; (* flags to restrict access to)
;
; When given a handle, unmaps (or restricts?) access to memory from that object
; from the address space mapped it.
; Otherwise, unmaps mapped memory and releases it back to its backer.
;
; TODO What happens to other processes that had mapped this range?
; In general: a process is not made aware of which parts of its memory are
; *mapped* in other places, but we do need to somehow manage frames that are
; currently in a page table somewhere.
;
; The system will know our-handle for anything that had memory mapped (through
; us) that is backed by the given vaddr. Maybe it could just (fail and) return
; the first extant user if it's still backed. Use the unmap call to steal the
; backing page back and remove access. Unmap is a misnomer for that though,
; it will just revoke access - the process keeps running and may fault to
; request the page be backed again.
MSG_UNMAP	equ	3

; Duplicate, rename or dissociate a handle.
;
; rdi: handle to duplicate/rename/dissociate
; rsi: new handle 1
; rdx: new handle 2
;
; If either rsi or rdx is already associated, they will be dissociated
; If rdi is not associated, both rsi and rdx will be dissociated
; If both rsi and rdx are 0, rdi will be dissociated
; If neither rsi or rdx are 0, rdi will be duplicated
; If only one of rsi/rdx are non-zero, this renames rdi to the non-zero value
;
; if 1 is a handle for object A,
; hmod(1, 1, 2) will end up with 1 remaining a handle for A and 2 being a
; fresh handle for the same process, except 2 will be unknown to A's process
; and not map to any handle on the remote side (yet).
; if 2 previously pointed anywhere (even in A's process), that association is
; forgotten and sending something to 2 will (even if it's the same process)
; not associate to any remote value.
; hmod(1, 2, 0) makes 2 a handle for A, keeping the remote association intact
; and disassociating 1 completely. Messages that would be received for 1 will
; now be received for 2.
MSG_HMOD	equ	4

; "Send" a message to a previously dissociated handle and create a process on
; it - the dissociated handle becomes the local handle for a new process.
; (TODO For an already-associated handle - should this fail or just dissociate
; the previous handle and create a new process for it?)
;
; rdi: new process handle
; rsi: entry-point for new process
; rdx: end of new process' load segment
;
; The stack for the new process will start just before the page that has the
; entry point.
;
; Returns:
; rax: success or error code
; The entry point will behave as if it just received the newproc message.
; rax: newproc message code
; rdi: process handle for parent process
; Remaining message registers are copied from the parent process.
MSG_NEWPROC	equ	5

; other ideas
; * patch/pipe/connect/forward/shortcut api:
;   take two of your handles, and connect their two remote endpoints directly
;   to each other bypassing yourself.
;   For processes A,B,C and handles x,y,z:
;   A(x) -> B(y), B(y) -> C(z)
;   will associate A(x) directly with C(z) instead of through B.
;   This allows e.g. a process to call the loader, and the loader to connect
;   a new child back to the original.
; * remote map/grant (fidgety because it allows poking in another process'
;   address space directly. Should find better ways around htat.)

; Some use-cases to consider:
; * Writing a process loader. You have a mappable file object, and a handle to
;   a designated loader service. (Let's call them F and L.)
;   1. Create a pair of fresh handles to the loader and file
;   2. Patch together the fresh handles
;   3. Tell L to start loading from F
;   (If the patch operation allows sending data to L's end while patching, this
;   would be a single operation.)
;   The loader now probably needs to create a new process and give that some
;   loader data and the file object to load from.

; backdoor call(s)
MSG_SYSCALL_WRITE	equ	6
MSG_SYSCALL_IO		equ	7

; Start of user-mapped message-type range
MSG_USER	equ	16
MSG_MAX		equ	255

; fields we need (to make the message the only "syscall number" thingy):
; * message code (1-2 bytes)
;   (errors might have message codes)
; * which phases to perform: send, rcv, sendrcv
;   two bits are probably most logical - but that leaves a possible message
;   where nothing is done, ergh! Could leave that be and it's just be a no-op,
;   or use it for "syscall"?
; * asynchronicity - but do we need it separately for send and rcv?
;   * async send: return immediately with one of those error message codes if
;     not ready to receive
;   * async recv: likewise if nothing is trying to send right now
;   * async sendrcv has two cases:
;     * not possible to send now: nothing was actually done, and we can't
;       really receive a message here - that'll lose all the data we were
;       trying to send.
;     * was possible to send, but not receive: something was done!
;     we would need to be clear about what happened.
;     at least one alternative is to not allow this. what are the odds both a
;     send and receive can complete asynchronously
;   * separate bits would allow more combinations (still requires indication on
;     which phase you're getting an error/wouldblock about!)
;     * a sync send to be followed by an async rcv
;       (e.g. we definitely need to send now, and then have something we'd like
;       to do if idle, but if not idle, give us that message instead)
;     * an async send with a sync rcv
;       if ready: sends and blocks until something received
;       if not ready: abort and don't receive anything
;   * How about async bits being set for phases that are not enabled?
;     Probably nonsense.
; * source filter specified:
;   * sends require a single target, no optional else
;   * receive:
;     no source means receive from anything. This is likely to be common.
;     given source means receive from exactly that, or (for a new handle) to
;     receive from anything or a fresh handle
;   * sendrcv doesn't need a source, but can take a source for the rcv phase

; The possible values for the parameters in "generic" forms:
;  - 0 = no-op
;  - -1 = any source (any target = error)
;  - other = specific source/target (or fresh source. fresh target not allowed.)
;
; The recipient of a message only ever sees the source of it, not the target it
; sent to. The source's target handle and the target's source handle are two
; endpoints of the same abstract entity. The target can't send anything to
; itself. (I think this motivates a single source/target/both as the best
; default.)
; 
; Common (probably) operations:
; * receive from all (sometimes allow new)
; * send to specific
; * replywait (sendrcv): to specific, from all (sometimes allow new)
; * call (sendrcv): to *and* from *same* specific
;
; Singular receive from specific is presumably rare. Asynchronous is always
; one-way.
;
; * receive: 1 all/new operand (00)
; * send: 1 operand, no all/new (01)
; * call: 1 operand, no all/new (10)
; * replywait: 1 operand. 1 all/new operand (11)

; let's have receive take an operand - the sender will get a sender as the
; first received parameter anyway.

; receive: "target" parameter is 0 for any known, other for new, known is error
MSG_RECV equ 0
; any non-zero message is a "send"!
; send: first parameter must be known
MSG_KIND_SEND equ 0x000
; call: first parameter is known, and reply will be received only from there
MSG_KIND_CALL equ 0x100
; replywait: first parameter is known, second parameter is 0 for any, known for other
; is there some kind of mapping between message registers here?
MSG_KIND_REPLYWAIT equ 0x200
; TODO message kind for errors

MSG_CODE_MASK equ 0xff
MSG_KIND_SHIFT equ 8

%define msg_code(msg, kind) ((kind) | (msg))
%define msg_send(msg) msg_code(msg, MSG_KIND_SEND)
%define msg_call(msg) msg_code(msg, MSG_KIND_CALL)
%define msg_reply(msg) msg_code(msg, MSG_KIND_REPLYWAIT)

%include "messages_con.inc"
%include "messages_io.inc"
%include "messages_irq.inc"
