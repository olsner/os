namespace proc {
enum ProcFlags {
// The process is currently queued on the run queue.
    Queued = 0,

// Can return to user-mode with sysret, only some registers will be restored:
// rsp, rip: restored to previous values
// rcx, r11: rip and rflags, respectively
// rax: syscall return value
// Remaining registers will be 0 (?)
    FastRet = 1,
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
u32 mask(ProcFlags flag) {
    return 1 << flag;
}
using x86::Regs;
using x86::SavedRegs;

struct Process {
    // First: fields shared with asm code...
    union {
        struct {
            Regs regs;
            u64 rip;
            u64 rflags;
        };
        SavedRegs saved_regs;
    };
    // End of assembly-shared fields.
    u32 flags;

    DListNode<Process> node;

    RefCnt<AddressSpace> aspace;
    RefCnt<Socket> blocked_socket;
    uintptr_t fault_addr;
    // TODO FXSave

    RefCnt<File> send_file;
    u64 recv_dest;

    Process(RefCnt<AddressSpace> aspace): aspace(std::move(aspace))
    {
        flags = 1 << FastRet;
        rflags = x86::rflags::IF;
    }

    u64 cr3() const {
        return aspace->cr3();
    }

    bool is(ProcFlags flag) const {
        return flags & mask(flag);
    }
    bool is_queued() const { return is(Queued); }

    void set(ProcFlags flag) {
        flags |= mask(flag);
    }
    void unset(ProcFlags flag) {
        flags &= ~mask(flag);
    }

    u64 ipc_state() const {
        return flags & (mask(InSend) | mask(InRecv));
    }
    bool is_runnable() const {
        return ipc_state() == 0;
    }
    bool is_blocked() const { return !is_runnable(); }

    const char *name() const { return aspace->name(); }

    void dump_regs() const { saved_regs.dump(); }

    bool can_receive_from(Socket* sock) {
        // blocked_socket = null => open-ended receive
        return ipc_state() == mask(proc::InRecv) && (blocked_socket == nullptr || blocked_socket == sock);
    }

    bool sending_to(Socket* sock) {
        return is(proc::InSend) && blocked_socket == sock;
    }

    void block_on_socket(const RefCnt<Socket>& sock);
};
}
