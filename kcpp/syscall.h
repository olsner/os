namespace syscall {

namespace {

enum syscalls_builtins {
    MSG_NONE = 0,
    SYS_RECV = MSG_NONE,
    SYS_MAP,
    SYS_PFAULT,
    SYS_UNMAP,
    SYS_HMOD,
    SYS_NEWPROC, // Not really used, ungood.
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
    SYS_YIELD = 10,

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
    log(hmod, "%s hmod: id=%lx rename=%lx copy=%lx\n", p->name(), id, rename, copy);
    AddressSpace* aspace = p->aspace.get();
    if (auto handle = aspace->find_handle(id)) {
        if (copy) {
            aspace->new_handle(copy, handle->otherspace);
        }
        if (rename) {
            aspace->rename_handle(handle, rename);
        } else {
            aspace->delete_handle(handle);
        }
    }
}

void transfer_set_handle(Process *target, Process *source) {
    auto rcpt = target->regs.rdi;
    auto from = source->regs.rdi;

    auto h = source->find_handle(from);
    if (!rcpt) {
        rcpt = h->other->key();
    } else if (!target->find_handle(rcpt)) {
        if (auto g = h->other) {
            rcpt = g->key();
            assert(g->other == h);
            log(transfer_message, "transfer_set_handle: g=%p, g.id=%lx\n",
                g, rcpt);
        } else {
            // Associate new handle
            g = target->new_handle(rcpt, source->aspace.get());
            g->associate(target->aspace.get(), h);
            assert(g->key() == rcpt);
        }
    } else {
        assert(rcpt == h->other->key());
    }
    log(transfer_message, "transfer_set_handle: rcpt=%lx for %lx from %lx\n",
            rcpt, target->regs.rdi, from);
    target->regs.rdi = rcpt;
}

NORETURN void syscall_return(Process *p, u64 res, u64 arg1 = 0, u64 arg2 = 0, u64 arg3 = 0) {
    getcpu().syscall_return(p, res, arg1, arg2, arg3);
}

NORETURN void transfer_message(Process *target, Process *source) {
    transfer_set_handle(target, source);
    log(transfer_message, "transfer_message %s <- %s\n", target->name(), source->name());

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

    Cpu& c = getcpu();
    c.queue(target);
    if (!source->ipc_state()) {
        target->remove_waiter(source);
        c.queue(source);
    } else {
        target->add_waiter(source);
    }
    c.run();
}

NORETURN void transfer_pulse(Process *target, uintptr_t key, uintptr_t events) {
    // Apparently we need the source process for transfer_set_handle, but we
    // already know the key that we should set.
    target->regs.rax = SYS_PULSE;
    target->regs.rdi = key;
    target->regs.rsi = events;

    target->unset(proc::InRecv);
    target->unset(proc::FastRet); // This really should be possible though
    assert(target->is_runnable());
    getcpu().switch_to(target);
}

void send_or_block(Process *sender, Handle *h, u64 msg, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    sender->regs.rax = msg;
    sender->regs.rdi = h->key();
    sender->regs.rsi = arg1;
    sender->regs.rdx = arg2;
    sender->regs.r8 = arg3;
    sender->regs.r9 = arg4;
    sender->regs.r10 = arg5;

    if (auto p = sender->aspace->pop_recipient(h)) {
        log(ipc, "send_or_block: %s sends to (specific) %s\n", sender->name(), p->name());
        transfer_message(p, sender);
    } else if (auto p = h->otherspace->pop_open_recipient()) {
        log(ipc, "send_or_block: %s sends to %s\n", sender->name(), p->name());
        transfer_message(p, sender);
    } else {
        log(ipc, "send_or_block: no available recipient, %s waits for %s\n", sender->name(), h->otherspace->name());
        sender->wait_for(h->otherspace);
    }
}

NORETURN void ipc_send(Process *p, u64 msg, u64 rcpt, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    auto handle = p->find_handle(rcpt);
    log(ipc, "%s ipc_send to %lx (%s)\n", p->name(), rcpt, handle ? handle->otherspace->name() : NULL);
    assert(handle);
    p->set(proc::InSend);
    send_or_block(p, handle, msg, arg1, arg2, arg3, arg4, arg5);
    log(ipc, "ipc_send: blocked\n");
    getcpu().run();
}

NORETURN void ipc_call(Process *p, u64 msg, u64 rcpt, u64 arg1, u64 arg2, u64 arg3 = 0, u64 arg4 = 0, u64 arg5 = 0) {
    auto handle = p->find_handle(rcpt);
    assert(handle);
    log(ipc, "%s ipc_call to %lx (%s)\n", p->name(), rcpt, handle->otherspace->name());
    p->set(proc::InSend);
    p->set(proc::InRecv);
    p->regs.rdi = rcpt;
    send_or_block(p, handle, msg, arg1, arg2, arg3, arg4, arg5);
    log(ipc, "ipc_call: blocked\n");
    getcpu().run();
}

NORETURN void ipc_recv(Process *p, u64 from) {
    auto handle = from ? p->find_handle(from) : nullptr;
    log(recv, "%s recv from %lx (%s)\n", p->name(), from,
            handle ? handle->otherspace->name() : "fresh");
    p->set(proc::InRecv);
    p->regs.rdi = from;
    if (auto sender = p->aspace->pop_sender(handle)) {
        log(recv, "%s recv: found sender %s\n", p->name(), sender->name());
        transfer_message(p, sender);
        // noreturn
    }
    if (handle) {
        log(recv, "%s recv: waiting for %s\n", p->name(), handle->otherspace->name());
        // TODO Handle cases where 'handle' is the handle with a pulse waiting.
        assert(!handle->events);
        handle->otherspace->add_waiter(p);
    } else {
        if (auto h = p->aspace->pop_pending_handle()) {
            uintptr_t events = latch(h->events);
            log(pulse, "%s recv: got events %lx from %lx\n", p->name(), events, h->key());
            transfer_pulse(p, h->key(), events);
        }

        Cpu &c = getcpu();
        if (c.irq_process == p && c.irq_delayed[0]) {
            auto irqs = latch(c.irq_delayed[0]);
            log(pulse, "%s recv: got pending IRQs %lx\n", p->name(), irqs);
            transfer_pulse(p, 0, irqs);
        }

        log(recv, "%s recv: found no senders\n", p->name());
        p->aspace->add_blocked(p);
    }
    getcpu().run();
}

NORETURN void syscall_pulse(Process *p, uintptr_t handle, uintptr_t bits) {
    auto h = p->find_handle(handle);
    log(pulse, "%s sending pulse %lx to %lx (%s)\n", p->name(), bits, handle,
            h ? h->otherspace->name() : "null");
    assert(h);
    if (!h || !h->other) {
        syscall_return(p, 0); // FIXME Error code
    }

    auto rcpt = p->aspace->pop_recipient(h);
    if (!rcpt) rcpt = h->otherspace->pop_open_recipient();
    if (rcpt) {
        uintptr_t send_bits = latch(h->other->events) | bits;
        log(pulse, "delivering %lx to %lu (%s)\n", send_bits, h->other->key(), rcpt->name());
        getcpu().queue(p);
        transfer_pulse(rcpt, h->other->key(), send_bits);
    } else {
        log(pulse, "pulse from %s not deliverable, saved in %s for later\n",
                p->name(), h->otherspace->name());
        h->otherspace->pulse_handle(h->other, bits);
        syscall_return(p, 0);
    }
}

NORETURN void syscall_map(Process *p, uintptr_t handle, uintptr_t flags, uintptr_t vaddr, uintptr_t offset, uintptr_t size) {
    using namespace aspace;

    // TODO (also unimpl in asm): remove any previously backed pages.
    // (not necessarily necessary - vaddr mappings and what's actually mapped
    // don't have to match, there's no syscall for unbacking pages though.)

    // TODO Check that offset & 4095 == 0, can't map unaligned memory
    // TODO Check that flags & ~4095 == 0, it'll change the offset otherwise

	// With handle = 0, the flags can be:
	// phys: raw physical memory mapping
	// anon: anonymous memory mapped on use
	// anon|phys: similar to anonymous, but the backing is allocated
	// immediately, the memory is "locked" (actually all allocations are),
	// and the phys. address of the memory (always a single page) is
	// returned in rax.

    // For DMA memory we want to allocate the memory right away so we can
    // return the address to the caller.
    if (!handle && (flags & MAP_DMA) == MAP_DMA) {
        // Maybe require that offset == 0, since it's currently not used but we
        // might find a use for it later? And then we'd not want stuff relying
        // on garbage being OK.
        offset = mem::allocate_frame();

        // TODO Check that size == 4096, otherwise we'll give the process
        // access to a whole bunch of extra physical memory.
    }

    uintptr_t end_vaddr = vaddr + size;
    p->aspace->map_range(vaddr, end_vaddr, handle, flags | (offset - vaddr));

    if (flags & MAP_PHYS) {
        syscall_return(p, offset);
    }
    syscall_return(p, 0);
}

NORETURN void syscall_pfault(Process *p, uintptr_t vaddr, uintptr_t flags) {
    // TODO Error out instead of silently adjusting the values.
    vaddr &= -4096;
    flags &= aspace::MAP_RWX;
    log(prefault, "%s prefault: %lx flags %lx\n", p->name(), vaddr, flags);

    p->fault_addr = vaddr | flags;

    // Look up mapping at vaddr, adjust vaddr to offset.
    uintptr_t offsetFlags, handle;
    if (!p->aspace->find_mapping(vaddr, offsetFlags, handle)) {
        // TODO Error code instead
        abort("No mapping for PFAULT");
    }

    auto h = p->find_handle(handle);
    assert(h);
    log(prefault, "%s prefault: mapped to %lx (%s) offset %lx\n", p->name(), handle, h->otherspace->name(), offsetFlags);
    p->set(proc::PFault);
    ipc_call(p, SYS_PFAULT, handle, offsetFlags & -4096, flags & offsetFlags);
}

// Respond to a PFAULT message from a process that's mapped some memory from
// us. This could be from a "prefault" syscall, or from the page fault
// exception handler.
NORETURN void syscall_grant(Process *p, uintptr_t handle, uintptr_t vaddr, uintptr_t flags) {
    using namespace aspace;

    // TODO Error out instead of adjusting
    flags &= MAP_RWX;

    auto h = p->find_handle(handle);
    if (!h) {
        abort("GRANT for unknown handle\n");
    }
    if (!h->other) {
        abort("GRANT for unassociated handle\n");
    }
    log(grant, "%s grant(%lx (%s) vaddr=%#lx flags=%lu)\n", p->name(), handle,
            h->otherspace->name(), vaddr, flags);

    // Find faulted process in otherspace
    auto rcpt = p->aspace->pop_pfault_recipient(h);
    if (!rcpt) {
        abort("No-one seems to be waiting for a page fault...\n");
    }
    if (!rcpt->is(proc::PFault)) {
        // Note that rcpt has already been popped from a waiting list, to
        // allow more progress we should add it back if we don't send anything.
        abort("GRANT target is not handling a fault\n");
    }
    auto fault_addr = rcpt->fault_addr;
    uintptr_t offsetFlags;
    uintptr_t mapped_handle;
    if (!rcpt->aspace->find_mapping(fault_addr, offsetFlags, mapped_handle)) {
        abort("Process fault addr is not mapped\n");
    }
    if (mapped_handle != h->other->key()) {
        abort("Process fault addr is mapped to the wrong handle\n");
    }
    flags &= offsetFlags;
    flags |= offsetFlags & MAP_NOCACHE;

    Backing *backing = p->aspace->find_backing(vaddr);
    uintptr_t paddr = backing->paddr();
    if (!paddr) {
        abort("Recursive fault required...\n");
    }
    // TODO Find previously mapped page, release it if necessary
    Sharing *sharing = p->aspace->find_add_sharing(vaddr, paddr);
    assert(sharing->paddr == paddr);
    h->otherspace->add_shared_backing(fault_addr | flags, sharing);
    // TODO Add the PTE for the newly added page to save a page fault

    rcpt->unset(proc::PFault);
    if (rcpt->is(proc::InRecv)) {
        // If this is set we got here from an explicit pfault syscall, so
        // respond with a message.
        p->regs.rax = MSG_KIND_SEND | SYS_GRANT;
        p->regs.rdi = handle;
        p->regs.rsi = vaddr;
        p->regs.rdx = flags;
        transfer_message(rcpt, p);
    }
    unimpl("grant after page fault");
}

NORETURN void syscall_yield(Process *p) {
    auto &cpu = getcpu();
    cpu.queue(p);
    cpu.run();
}

} // namespace

extern "C" void syscall(u64, u64, u64, u64, u64, u64, u64) NORETURN;

#define SC_UNIMPL(name) case SYS_##name: unimpl(#name)

NORETURN void syscall(u64 arg0, u64 arg1, u64 arg2, u64 arg5, u64 arg3, u64 arg4, u64 nr) {
    auto p = getcpu().process;
    log(syscall, "%s: syscall %#x: %lx %lx %lx %lx %lx %lx\n",
            p->name(),
            (unsigned)nr, arg0, arg1, arg2, arg3, arg4, arg5);
    getcpu().leave(p);
    p->set(proc::FastRet);

    switch (nr) {
    case SYS_RECV:
        ipc_recv(p, arg0);
        break;
    case SYS_MAP:
        syscall_map(p, arg0, arg1, arg2, arg3, arg4);
        break;
    case SYS_PFAULT:
        /* First argument (arg0) is not used. */
        syscall_pfault(p, arg1, arg2);
        break;
    SC_UNIMPL(UNMAP);
    case SYS_HMOD:
        hmod(p, arg0, arg1, arg2);
        syscall_return(p, 0);
        break;
    SC_UNIMPL(NEWPROC);
    case SYS_WRITE:
        Console::write(arg0, true);
        syscall_return(p, 0);
        break;
    case SYS_IO:
        syscall_return(p, portio(arg0, arg1, arg2));
        break;
    case SYS_GRANT:
        syscall_grant(p, arg0, arg1, arg2);
        break;
    case SYS_PULSE:
        syscall_pulse(p, arg0, arg1);
        break;
    case SYS_YIELD:
        syscall_yield(p);
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
