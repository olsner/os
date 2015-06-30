namespace proc {
enum ProcFlags {
// The process is currently queued on the run queue.
    Queued = 0,

// Can return to user-mode with sysret, only some registers will be restored:
// rsp, rip: restored to previous values
// rcx, r11: rip and rflags, respectively
// rax: syscall return value
// Remaining registers will be 0 (?)
    PROC_FASTRET = 1,
// IN_RECV: Similar to FASTRET, when waiting for a message-send rendezvous
// When set together with IN_SEND, it's a sendrcv and the SEND needs to finish
// first.
// At any time when IN_RECV is set, the proc's saved rdi contains a pointer to
// the handle being received from.
// When a process starts a receive, until it becomes properly blocked on some
// process or finishes the receive immediately, it will be both RUNNING and
// IN_RECV.
    InRecv = 2,
// Process is trying to do a synchronous send or sendrcv, blocking on the
// waiting_for process to reach a PROC_IN_RECV state. Both IN_SEND and IN_RECV
// can be set at the same time.
// At any time when IN_SEND is set, the proc's saved rdi contains a pointer to
// the handle being sent to.
// When a process starts a send, until it becomes properly blocked on some
// process or finishes the operation, it will be both RUNNING and IN_SEND.
    InSend = 3,
// Is the currently running process
    Running = 4,
// Process has had a page fault that requires a response from a backer, or has
// requested a page paged in.
// proc.fault_addr is the address that faulted/was requested.
    PFault = 5

};
struct Regs {
    u64 rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
};
struct Process {
    // First: fields shared with asm code...
    Regs regs;
    u64 rip;
    u64 rflags;
    u64 cr3;
    // End of assembly-shared fields.
    u64 flags;
    Process *waiting_for;

    // TODO DList<Process> waiters;
    // TODO DListNode<Process> waiters_node;

    AddressSpace *aspace;
    // TODO Dict<Handle> handles;
    // TODO Dict<PendingPulse> pending;
    u64 fault_addr;
    // TODO FXSave

    Process(AddressSpace *aspace):
        aspace(aspace)
    {
        flags = PROC_FASTRET;
        cr3 = aspace->cr3();
        rflags = x86::rflags::IF;
    }

    void assoc_handles(uintptr_t j, Process *other, uintptr_t i) {
    }
};
}
