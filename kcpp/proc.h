#include <utility>

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
using x86::Regs;
using x86::SavedRegs;

// Process fields shared with assembly code
struct ProcessAsm {
    union {
        struct {
            Regs regs;
            u64 rip;
            u64 rflags;
            u64 cr3;
        };
        SavedRegs saved_regs;
    };
};

struct Process: ProcessAsm {
    u64 flags;

    DListNode<Process> node;

    const RefCnt<AddressSpace> aspace;
    AddressSpace *waiting_for;
    uintptr_t fault_addr;
    // TODO FXSave

    Process(RefCnt<AddressSpace> aspace):
        aspace(std::move(aspace))
    {
        flags = 1 << FastRet;
        cr3 = aspace->cr3();
        rflags = x86::rflags::IF;
    }

    // TODO static/global make_handle_pair or something
    void assoc_handles(uintptr_t j, Process *other, uintptr_t i) {
        auto otherspace = other->aspace;
        auto x = aspace->new_handle(j, &*otherspace);
        auto y = otherspace->new_handle(i, &*aspace);
        x->other = y;
        y->other = x;
    }

    Handle *find_handle(uintptr_t key) const {
        return aspace->find_handle(key);
    }
    void rename_handle(Handle *handle, uintptr_t new_key) {
        aspace->rename_handle(handle, new_key);
    }
    void delete_handle(Handle *handle) {
        aspace->delete_handle(handle);
    }

    void wait_for(AddressSpace *otherspace) {
        otherspace->add_waiter(this);
    }
    void add_waiter(Process *other) {
        other->wait_for(&*aspace);
    }
    void remove_waiter(Process *other) {
        aspace->remove_waiter(other);
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
    bool is_runnable() const {
        return ipc_state() == 0;
    }
    bool is_blocked() const { return !is_runnable(); }

    const char *name() const { return aspace->name(); }

    void dump_regs() const { saved_regs.dump(); }
};
}

namespace aspace {

Process *AddressSpace::pop_sender(Handle *target) {
    for (auto p: waiters) {
        if (p->is(proc::InSend)
            && (!target
                || (p->aspace == target->otherspace
                    && target->other->key() == p->regs.rdi))) {
            remove_waiter(p);
            return p;
        }
    }

    return nullptr;
}

Process *AddressSpace::pop_recipient(Handle *source) {
    return pop_recipient(source, proc::mask(proc::InRecv));
}
Process *AddressSpace::pop_pfault_recipient(Handle *source) {
    return pop_recipient(source, proc::mask(proc::InRecv) | proc::mask(proc::PFault));
}
Process *AddressSpace::pop_recipient(Handle *source, uintptr_t ipc_state) {
    // Iterate waiters, find a "remote" process that can receive something from
    // source.
    Handle *other = source->other;
    for (auto p: waiters) {
        if (p->ipc_state() == ipc_state) {
            auto rcpt = p->find_handle(p->regs.rdi);
            log(waiters,
                    "%s get_recipient(%#lx): found %s receiving from %p (looking for %p)\n",
                    name(), source->key(), p->name(), rcpt, other);
            // If there was no handle, we don't expect to find that process
            // here - pop_open_recipient would be used for that.
            assert(rcpt);
            if (!rcpt || rcpt == other) {
                remove_waiter(p);
                return p;
            }
        } else {
            log(waiters, "%s get_recipient(%#lx): found %s in state %lu (wanted %lu)\n",
                    name(), source->key(), p->name(), p->ipc_state(), ipc_state);
        }
    }
    log(waiters, "%s get_recipient(%#lx): no match\n", name(), source->key());
    return nullptr;
}

Process *AddressSpace::pop_open_recipient() {
    for (auto p: blocked) {
        if (p->ipc_state() == proc::mask(proc::InRecv)) {
            log(waiters, "%s get_recipient(): found %s\n", name(), p->name());
            // If it's receiving to a specific handle, we shouldn't find it
            // here but in the other address space's waiter list.
            assert(!p->find_handle(p->regs.rdi));
            remove_blocked(p);
            return p;
        }
    }
    log(waiters, "%s get_recipient(): no open recipients\n", name());
    return nullptr;
}

void AddressSpace::add_waiter(Process *p)
{
    log(waiters, "%s adds waiter %s\n", name(), p->name());
    assert(!p->waiting_for);
    assert(!p->is_queued());
    waiters.append(p);
    p->waiting_for = this;
}
void AddressSpace::remove_waiter(Process *p)
{
    log(waiters, "%s removes waiter %s\n", name(), p->name());
    assert(p->waiting_for == this || p->waiting_for == nullptr);
    waiters.remove(p);
    p->waiting_for = nullptr;
}
void AddressSpace::add_blocked(Process *p)
{
    log(waiters, "%s is now blocked on %s\n", p->name(), name());
    assert(!p->waiting_for);
    assert(!p->is_queued());
    blocked.append(p);
    p->waiting_for = this;
}
void AddressSpace::remove_blocked(Process *p)
{
    log(waiters, "%s unblocked\n", p->name());
    assert(p->waiting_for == this);
    blocked.remove(p);
    p->waiting_for = nullptr;
}
}
