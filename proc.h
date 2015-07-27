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
u64 mask(ProcFlags flag) {
    return 1 << flag;
}
struct Regs {
    u64 rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
};

struct Process;

struct Handle
{
    typedef uintptr_t Key;
    DictNode<Key, Handle> node;
    Process *process;
    Handle *other;
    u64 pulses;

    Handle(uintptr_t key, Process *process):
        node(key), process(process), other(NULL), pulses(0) {}

    uintptr_t key() const { return node.key; }

    void dissociate() {
        if (other) {
            other->other = NULL;
            other = NULL;
        }
    }

    void associate(Handle *g) {
        unimpl("associate");
    }
};

struct PendingPulse
{
    typedef uintptr_t Key;
    DictNode<Key, PendingPulse> node;

    PendingPulse(Handle *handle): node(handle->key()) {}
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

    DList<Process> waiters;
    DListNode<Process> node;

    AddressSpace *aspace;
    Dict<Handle> handles;
    Dict<PendingPulse> pending;
    u64 fault_addr;
    // TODO FXSave

    Process(AddressSpace *aspace):
        aspace(aspace)
    {
        flags = 1 << FastRet;
        cr3 = aspace->cr3();
        rflags = x86::rflags::IF;
    }

    void assoc_handles(uintptr_t j, Process *other, uintptr_t i) {
//        AddressSpace *otherspace = other->aspace;
//        auto x = aspace->new_handle(j, other);
//        auto y = otherspace->new_handle(i, this);
        auto x = new_handle(j, other);
        auto y = other->new_handle(i, this);
        x->other = y;
        y->other = x;
    }

    Handle *new_handle(uintptr_t key, Process *other) {
        if (Handle *old = handles.find_exact(key)) {
            delete_handle(old);
        }
        return handles.insert(new Handle(key, other));
    }

    Handle *find_handle(uintptr_t key) const {
        return handles.find_exact(key);
    }
    void rename_handle(Handle *handle, uintptr_t new_key) {
        handle->node.key = new_key;
        // Update collections with new key.
    }
    void delete_handle(Handle *handle) {
        handle->dissociate();
        Handle* existing = handles.remove(handle->key());
        assert(existing == handle);
        delete handle;
    }

    void add_waiter(Process *other) {
        waiters.append(other);
    }
    void remove_waiter(Process *other) {
        waiters.remove(other);
    }

    bool is(ProcFlags flag) const {
        return flags & (1 << flag);
    }
    bool is_queued() const { return is(Queued); }

    void set(ProcFlags flag) {
        flags |= (1 << flag);
    }
    void unset(ProcFlags flag) {
        flags &= ~(1 << flag);
    }

    u64 ipc_state() const {
        return flags & (mask(InSend) | mask(InRecv) | mask(PFault));
    }
};
}
