namespace syscall {

namespace {

enum syscalls_builtins {
    MSG_NONE = 0,
    SYS_RECV = MSG_NONE,
    SYS_MAP,
    SYS_PFAULT,
    SYS_UNMAP,
    SYS_HMOD,
    SYS_WRITE = 6,
    // arg0 (dst) = port
    // arg1 = flags (i/o) and data size:
    //  0x10 = write (or with data size)
    //  0x01 = byte
    //  0x02 = word
    //  0x04 = dword
    // arg2 (if applicable) = data for output
    SYS_IO = 7, // Backdoor!
    SYS_GRANT = 8,
    SYS_PULSE = 9,

    MSG_USER = 16,
    MSG_MASK = 0xff,
};

enum msg_kind {
    MSG_KIND_MASK = 0x300,
    MSG_KIND_SEND = 0x000,
    MSG_KIND_CALL = 0x100,
};

u64 portio(u16 port, u8 op, u32 data) {
    log(portio, "portio: port=%x op=%x data=%x\n", port, op, data);
    u32 res = 0;
    switch (op) {
    case 0x01: asm("inb %%dx, %%al" : "=a"(res) : "d"(port)); break;
    case 0x02: asm("inw %%dx, %%ax" : "=a"(res) : "d"(port)); break;
    case 0x04: asm("inl %%dx, %%eax" : "=a"(res) : "d"(port)); break;
    case 0x11: asm("outb %%al, %%dx" :: "a"(data), "d"(port)); break;
    case 0x12: asm("outw %%ax, %%dx" :: "a"(data), "d"(port)); break;
    case 0x14: asm("outl %%eax, %%dx" :: "a"(data), "d"(port)); break;
    }
    if ((op & 0x10) == 0) {
        log(portio, "portio: res=%x\n", res);
    }
    return res;
}

void hmod(Process *p, uintptr_t id, uintptr_t rename, uintptr_t copy) {
    log(hmod, "%p hmod: id=%lx rename=%lx copy=%lx\n", p, id, rename, copy);
    if (auto handle = p->find_handle(id)) {
        if (copy) {
            p->new_handle(copy, handle->process);
        }
        if (rename) {
            p->rename_handle(handle, rename);
        } else {
            p->delete_handle(handle);
        }
    }
}

using proc::Handle;

void transfer_set_handle(Process *target, Process *source) {
    auto rcpt = target->regs.rdi;
    auto from = source->regs.rdi;

    auto h = source->find_handle(from);
    if (!rcpt) {
        rcpt = h->other->key();
    } else if (!target->find_handle(rcpt)) {
        if (auto g = h->other) {
            rcpt = g->key();
            assert(g->other == g);
            log(transfer_message, "transfer_set_handle: g=%p, g.id=%lx\n",
                g, rcpt);
        } else {
            // Associate new handle
            g = target->new_handle(rcpt, source);
            g->associate(h);
            assert(g->key() == rcpt);
        }
    } else {
        assert(rcpt == h->other->key());
    }
    log(transfer_message, "transfer_set_handle: rcpt=%lx for %lx from %lx\n",
            rcpt, target->regs.rdi, from);
    target->regs.rdi = rcpt;
}

NORETURN void transfer_message(Process *target, Process *source) {
    transfer_set_handle(target, source);
    log(transfer_message, "transfer_message %p <- %p\n", target, source);

    target->regs.rax = source->regs.rax;
    target->regs.rsi = source->regs.rsi;
    target->regs.rdx = source->regs.rdx;
    target->regs.r8 = source->regs.r8;
    target->regs.r9 = source->regs.r9;
    target->regs.r10 = source->regs.r10;

    target->unset(proc::InRecv);
    target->unset(proc::FastRet);
    source->unset(proc::InSend);
    source->remove_waiter(target);

    auto c = getcpu();
    c.queue(target);
    if (!source->ipc_state()) {
        target->remove_waiter(source);
        c.queue(source);
    }
    c.run();
}

void send_or_block(Process *sender, Handle *h, u64 msg, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    sender->regs.rax = msg;
    sender->regs.rdi = h->key();
    sender->regs.rsi = arg1;
    sender->regs.rdx = arg2;
    sender->regs.r10 = arg3;
    sender->regs.r8 = arg4;
    sender->regs.r9 = arg5;

    u64 other_id = 0;
    if (auto other = h->other) other_id = other->key();

    auto p = h->process;
    if (p->ipc_state() == proc::mask(proc::InRecv)) {
        auto rcpt = p->regs.rdi;
        if (rcpt == other_id || !p->find_handle(rcpt)) {
            transfer_message(p, sender);
        }
    }

    log(ipc, "send_or_block: %p waits for %p\n", sender, p);
    p->add_waiter(sender);
}

NORETURN void ipc_send(Process *p, u64 msg, u64 rcpt, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    auto handle = p->find_handle(rcpt);
    log(ipc, "%p ipc_send to %lx ==> %p (%p)\n", p, rcpt, handle, handle ? handle->process : NULL);
    assert(handle);
    p->set(proc::InSend);
    send_or_block(p, handle, msg, arg1, arg2, arg3, arg4, arg5);
    log(ipc, "ipc_send: blocked\n");
    getcpu().run();
}

NORETURN void ipc_call(Process *p, u64 msg, u64 rcpt, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    auto handle = p->find_handle(rcpt);
    assert(handle);
    log(ipc, "%p ipc_call to %lx ==> process %p\n", p, rcpt, handle->process);
    p->set(proc::InSend);
    p->set(proc::InRecv);
    p->regs.rdi = rcpt;
    send_or_block(p, handle, msg, arg1, arg2, arg3, arg4, arg5);
    log(ipc, "ipc_call: blocked\n");
    getcpu().run();
}

void recv(Process *p, Handle *handle) {
    auto other_id = handle->other->key();
    auto rcpt = handle->process;
    if (rcpt->is(proc::InSend) && other_id == rcpt->regs.rdi) {
        transfer_message(p, rcpt);
        // doesn't return
    } else {
        rcpt->add_waiter(p);
    }
}

template <typename T>
T latch(T& var, T value = T()) {
    T res = var;
    var = value;
    return res;
}

void recv_from_any(Process *p, u64 id) {
    for (auto waiter: p->waiters) {
        log(recv, "%p recv: found waiter %p flags %lx\n", p, waiter, waiter->flags);
        if (waiter->is(proc::InSend)) {
            log(recv, "%p recv: found sender %p\n", p, waiter);
            p->remove_waiter(waiter);
            transfer_message(p, waiter);
        }
    }

#if 0
    if (auto h = p->pop_pending_handle()) {
    }
#endif

#if 0
    auto c = getcpu();
    if (c->irq_process == p && c->irq_delayed) {
        auto irqs = latch(c->irq_delayed);
        deliver_pulse(p, 0, irqs);
    }
#endif
}

NORETURN void ipc_recv(Process *p, u64 from) {
    auto handle = from ? p->find_handle(from) : nullptr;
    log(recv, "%p recv from %lx\n", p, from);
    p->set(proc::InRecv);
    p->regs.rdi = from;
    if (handle) {
        log(recv, "==> process %p\n", handle->process);
        recv(p, handle);
    } else {
        log(recv, "==> fresh\n");
        recv_from_any(p, from);
    }
    log(recv, "%p recv: nothing to receive\n", p);
    getcpu().run();
}

NORETURN void syscall_return(Process *p, u64 res) {
    getcpu().syscall_return(p, res);
}

} // namespace

extern "C" void syscall(u64, u64, u64, u64, u64, u64, u64) NORETURN;

NORETURN void syscall(u64 arg0, u64 arg1, u64 arg2, u64 arg5, u64 arg3, u64 arg4, u64 nr) {
    printf("syscall %#x: %lx %lx %lx %lx %lx %lx\n", (unsigned)nr, arg0, arg1, arg2, arg3, arg4, arg5);
    auto p = getcpu().process;
    getcpu().leave(p);
    p->set(proc::FastRet);

    switch (nr) {
    case SYS_RECV:
        ipc_recv(p, arg0);
        break;
    case SYS_MAP:
        unimpl("map");
        break;
    case SYS_HMOD:
        hmod(p, arg0, arg1, arg2);
        syscall_return(p, 0);
        break;
    case SYS_IO:
        syscall_return(p, portio(arg0, arg1, arg2));
        break;
    default:
        if (nr >= MSG_USER) {
            if ((nr & MSG_KIND_MASK) == MSG_KIND_SEND) {
                ipc_send(p, nr, arg0, arg1, arg2, arg3, arg4, arg5);
            } else if ((nr & MSG_KIND_MASK) == MSG_KIND_CALL) {
                ipc_call(p, nr, arg0, arg1, arg2, arg3, arg4, arg5);
            } else {
                abort("unknown IPC kind");
            }
        } else {
            abort("unhandled syscall");
        }
    }
}

} // system
