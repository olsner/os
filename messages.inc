; vim:filetype=nasm:

; Tries to fit into the SysV syscall ABI for x86-64.
; Message registers: rsi, rdx, r8, r9, r10
; (rcx and r11 are used by syscall.)

; Placeholder for e.g. no message received
; (Also overloaded with the "receive syscall")
MSG_NONE	equ	0

; Map a page of this object (syscall)
; * handle to map (or NULL if e.g. making an anonymous mapping)
; * flags:
;   * r/w/x access
;   * (temporary:) anon mapping - gets filled from kernel page-frame allocator
;     on fault.
;   * plus some arbitrary flags? there's a lot of space for flags (but we'll
;     need twice the space to account for requested/actual flags)
; * virtual address on receiving side
; * offset inside object
; * length to map
;   does not matter except for management of local address space - we'll
;   notice at fault time if there's no such page.
;
; Registers the handle and offset in your virtual address space. Future memory
; access may result in faults getting sent to the named handle.
MSG_MAP		equ	1

; MSG_PFAULT: Received when a fault happens in a process that has mapped a page
; from you, can also be sent to explicitly fault-in a page you've mapped.
;
; Parameters:
; * handle (in local address space) of mapped object
; * offset inside object
; * requested access (r/w/x)
; (* length in bytes - or always exactly one page - might be needed for the size
; of the page though...)
;
; For an explicit request to page-in memory, handle should be 0 and the offset
; replaced with the virtual address.
;
; Reply with a MSG_GRANT.
MSG_PFAULT	equ	2

; MSG_GRANT: Reply to a MSG_PFAULT message.
;
; Parameters:
; * handle (your handle to the mapper process)
; * offset is replaced with the local vaddr of the unfaulted page
;   this page will be shared with the receiving process:
;   * if currently mapped to a physical page, the same frame is reused
;   * if mapped recursively to another object, we will take a detour to fault
;     the page in before forwarding the result to the caller.
;   (but flags may still limit access to that memory)
; * flags are the actual access the process should receive
;   * should be, but doesn't have to be, a subset of the flags that were
;     requested (for example a read access to a page where RW access is
;     allowed - we can send both to avoid a future page fault)
;   * all access you hand out must be provided in the backing, this may cause
;     a detour to e.g. CoW and whatever's needed to give you read/write access.
;   * that seems to mean that for a potentially CoW page you should only give
;     exactly the access that was asked for, but for a known backed page give
;     all the access that is possible.
;   * the X flag is ignored here: if you give Read access you also give execute
;     access. It's up to the mapper's mapping whether it should be execute.
MSG_GRANT	equ	8

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
; rsi: renamed handle
; rdx: duplicated dissociated handle
;
; The end result is that rsi's handle (if not null) is an exact duplicate of
; rdi (linked to the same remote handle), rdi is destroyed (if not same as
; rsi), and rdx (if not null) is a fresh handle pointing to the same process
; that rdi pointed to.
;
; rdi must be a handle. (But does not have to be associated - in this case,
; rsi and rdx have the same function.)
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
;MSG_GRANT		equ	8

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
