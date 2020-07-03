#include "msg_syscalls.h"
#include "errno.h"
#include "socket.h"

namespace syscall {

namespace {

NORETURN void syscall_return(Process *p, u64 res, u64 arg1 = 0, u64 arg2 = 0, u64 arg3 = 0, u64 arg4 UNUSED = 0, u64 arg5 UNUSED = 0) {
    getcpu().syscall_return(p, res, arg1, arg2, arg3 /* TODO , arg4, arg5*/);
}

static inline void check_error(bool flag, Process *p, int res) {
    if (!(flag)) {
        syscall_return(p, res);
    }
}
#if 0
#define check_error(flag, p, res) assert(flag)
#endif

NORETURN void syscall_portio(Process *p, u16 port, u8 op, u32 data) {
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
    syscall_return(p, res);
}

NORETURN void syscall_hmod(Process *p, int fd, int rename, int copy) {
    log(hmod, "%s hmod: fd=%d rename=%d copy=%d\n", p->name(), fd, rename, copy);
    const auto aspace = p->aspace;
    if (auto file = aspace->get_file(fd)) {
        aspace->replace_file(fd, nullptr);
        assert(copy < 512);
        assert(rename < 512);
        // TODO 0 is not the "null" file, check for >=0 instead
        // Also need more variants to allow it being used for dup(), dup2() and dup3().
        if (copy) {
            aspace->replace_file(copy, file);
        }
        if (rename) {
            aspace->replace_file(rename, std::move(file));
        }
        syscall_return(p, 0);
    }
    syscall_return(p, -EBADF);
}

void store_message(Process *p, u64 msg, u64 dest, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    p->regs.rax = msg;
    p->regs.rdi = dest;
    p->regs.rsi = arg1;
    p->regs.rdx = arg2;
    p->regs.r8 = arg3;
    p->regs.r9 = arg4;
    p->regs.r10 = arg5;
}

NORETURN void transfer_pulse(Process *target, Process *source, int fd, uintptr_t events) {
    log(pulse, "%s <- %s: %d events %#lx\n", target->name(), source ? source->name() : nullptr, fd, events);

    target->unset(proc::InRecv);
    assert(target->is_runnable());
    syscall_return(target, SYS_PULSE, fd, events);
}

NORETURN void transfer_message(Process *target, Process *source, u64 msg, u64 dest, u64 arg1, u64 arg2, u64 arg3 = 0, u64 arg4 = 0, u64 arg5 = 0) {
    log(transfer_message, "%s <- %s: %lx %lx/%d args %ld %ld %ld %ld %ld\n", target->name(), source->name(), msg, dest >> 32, msg_dest_fd(dest), arg1, arg2, arg3, arg4, arg5);

    target->unset(proc::InRecv);
    source->unset(proc::InSend);

    assert(!target->ipc_state());
    assert(!target->blocked_socket);

    // Blocking sends always switch directly. Maybe there needs to be a per-cpu
    // flag to signal doing scheduling on the next system call.
    // If the source is blocked, there can't be an fd to send (as that only happens on the return).
    if (source->ipc_state()) {
        log(transfer_message, "direct (source blocked)\n");
        syscall_return(target, msg, dest, arg1, arg2, arg3, arg4 /* TODO , arg5 */);
    }

    // TODO I think this has to be done at a higher level - we don't know if
    // the source consented to having its file taken down here, and
    // transfer_message happens in more than one place.
    // Done after testing source->ipc_state() to implicitly check if this is
    // the request or response part of a transaction. It's only in the response
    // part that the fd should really be transferred.
    if (dest & MSG_TX_FD) {
        log(transfer_message, "used acceptfd (%s) %ld\n", dest & MSG_TX_CLOSEFD ? "close" : "leave", arg1);
        RefCnt<File> transfer_fd = std::move(source->send_file);

        // This needs to be checked earlier and produce an error.
        assert(transfer_fd);
        transfer_fd->owner = target;
        arg1 = target->aspace->add_file(std::move(transfer_fd));
        log(transfer_message, "target fd %ld\n", arg1);
    }

    // If source isn't blocking, add both target and source to run queue and
    // schedule. If the scheduling decision would've been 'target' anyway
    // though, this unnecessarily leaves the happy path.

    store_message(target, msg, dest, arg1, arg2, arg3, arg4, arg5);

    log(transfer_message, "delayed\n");

    assert(!source->blocked_socket);

    Cpu& c = getcpu();
    c.queue(target);
    c.queue(source);
    c.run();
}

NORETURN void ipc_call(Process *p, u64 msg, u64 dest, u64 arg1, u64 arg2, u64 arg3 = 0, u64 arg4 = 0, u64 arg5 = 0) {
    const int fd = msg_dest_fd(dest);

    log(ipc, "%s call %ld to %lx/%d args %ld %ld\n", p->name(), msg, dest >> 32, fd, arg1, arg2);

    const auto sock = p->aspace->get_socket(fd);
    check_error(sock, p, -EBADF);
    const RefCnt<Socket> other_side(sock->other_side); // may be weak ref, convert to strong

    p->set(proc::InRecv);

    // TODO Start the transaction before finding a recipient, so that we can
    // get the error check on this end when running out of transactions.

    if (const auto recipient = other_side->get_recipient()) {
        log(ipc, "%s call to %s\n", p->name(), recipient->name());
        recipient->blocked_socket = nullptr;

        if (u64 txid = other_side->start_transaction(p, dest)) {
            int fd = msg_dest_fd(recipient->recv_dest);
            if (fd < 0) {
                fd = recipient->aspace->get_file_number(other_side);
            }
            txid = msg_set_fd(txid, fd);
            transfer_message(recipient, p, msg, txid, arg1, arg2, arg3, arg4, arg5);
        }
        else {
            other_side->add_waiter(recipient);
            log(ipc, "call: no transaction slots available\n");
            syscall_return(p, -EAGAIN);
        }
    }

    log(ipc, "call: blocked on %p (-> %p)\n", sock.get(), other_side.get());
    store_message(p, msg, dest, arg1, arg2, arg3, arg4, arg5);

    p->set(proc::InSend);
    p->recv_dest = dest;
    sock->add_waiter(p);

    getcpu().run();
}

void ipc_try_recv(Process *p, u64 from, RefCnt<Socket> sock) {
    const int fd = msg_dest_fd(from);

    if (uintptr_t send_bits = sock->get_reset_event_bits()) {
        log(ipc, "recv: %s finishing receive of pulses %#lx from %d\n", p->name(), send_bits, fd);
        transfer_pulse(p, nullptr, fd, send_bits);
    }

    const RefCnt<Socket> other_side(sock->other_side); // may be weak ref, convert to strong
    if (auto sender = other_side->get_sender()) {
        sender->blocked_socket = nullptr;

        u64 txid = from;
        // If it's a sendrcv, start the transaction now. If we don't have a
        // sender in mind, we can't "start" a transaction since we don't know
        // if the message received will be a transaction or not.
        // InSend is implied by get_sender returning the process.
        // (TODO Think about concurrent message delivery?)
        if (sender->is(proc::InRecv)) {
            if ((txid = sock->start_transaction(sender, sender->recv_dest))) {
                txid = msg_set_fd(txid, fd);
            }
            else {
                // Didn't successfully send to the sender, add it to waiters and keep going.
                other_side->add_waiter(sender);
                return;
            }
        }
        log(ipc, "recv: %s finishing receive from %s\n", p->name(), sender->name());
        transfer_message(p, sender,
                // msg, txid
                sender->regs.rax, txid,
                // arg1..5
                sender->regs.rsi, sender->regs.rdx,
                sender->regs.r8, sender->regs.r9, sender->regs.r10);
    }
    else {
        log(try_recv, "%s trying %p <- %p, no sender waiting\n", p->name(), sock.get(), other_side.get());
        Process* other_proc = other_side->owner;
        log(try_recv, "other side owner %s state %lx blocked on %p (sending_to = %d)\n", other_proc->name(), other_proc->ipc_state(), other_proc->blocked_socket.get(), other_proc->sending_to(other_side.get()));
    }
}

// Ugly stuff! Open-ended receives should be handled properly instead of
// iterating all open file descriptors to find something.
void ipc_recv_any(Process* p, u64 from) {
    Cpu& cpu = getcpu();
    if (p == cpu.irq_process) {
        if (auto irqs = latch(cpu.irq_delayed[0])) {
            log(irq, "recv: delivering IRQs %lx to %s\n", irqs, p->name());
            transfer_pulse(p, nullptr, -1, irqs);
        }
    }

    const int num_files = p->aspace->get_num_files();
    for (int fd = 0; fd < num_files; fd++) {
        if (auto sock = p->aspace->get_socket(fd)) {
            ipc_try_recv(p, msg_set_fd(from, fd), std::move(sock));
        }
    }
}

NORETURN void ipc_recv(Process *p, u64 from) {
    const int fd = msg_dest_fd(from);
    log(ipc, "recv: %s recv %d flags %lx\n", p->name(), fd, from >> 32);

    if (fd < 0) {
        // returns into the process if it finds something to deliver, only
        // returns here if nothing was found.
        ipc_recv_any(p, from);

        log(ipc, "recv: %s waiting for whatever\n", p->name());
        p->set(proc::InRecv);
        p->blocked_socket = nullptr;
        p->recv_dest = from;
        getcpu().run();
    }

    const auto sock = p->aspace->get_socket(fd);
    check_error(sock, p, -EBADF);

    // FIXME Leaks reference to sock when it does a syscall-return or context switch...
    ipc_try_recv(p, from, sock);

    const auto other_side = sock->other_side;
    p->set(proc::InRecv);
    p->recv_dest = from;
    sock->add_waiter(p);

    log(ipc, "recv: %s blocking on receive from %d (%p -> %p)\n", p->name(), fd, other_side, sock.get());
    getcpu().run();
}

NORETURN void ipc_send(Process *p, u64 msg, u64 dest, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    const int fd = msg_dest_fd(dest);
    const auto sock = p->aspace->get_socket(fd);
    log(ipc, "%s send %ld to %d args %ld %ld\n", p->name(), msg, fd, arg1, arg2);
    check_error(sock, p, -EBADF);

    if (dest & MSG_TX_FD) {
        p->send_file = dest & MSG_TX_CLOSEFD ? p->aspace->replace_file(arg1, nullptr) : p->aspace->get_file(arg1);
        check_error(p->send_file, p, -EBADF);
    }

    // TODO we should have a way to say that the transaction matched but the
    // wrong flags were used, other than just not finishing it. Perhaps that
    // match check should be done up here instead of in end_transaction.
    if (Transaction tx = sock->end_transaction(dest)) {
        assert(!tx.peer->blocked_socket);
        transfer_message(tx.peer, p, msg, tx.id, arg1, arg2, arg3, arg4, arg5);
    }

    const RefCnt<Socket> other_side(sock->other_side); // may be weak ref, convert to strong
    if (const auto recipient = other_side->get_recipient()) {
        recipient->blocked_socket = nullptr;

        u64 txid = recipient->recv_dest;
        if (msg_dest_fd(txid) < 0) {
            txid = msg_set_fd(txid, recipient->aspace->get_file_number(other_side));
        }
        transfer_message(recipient, p, msg, txid, arg1, arg2, arg3, arg4, arg5);
    }

    store_message(p, msg, dest, arg1, arg2, arg3, arg4, arg5);

    p->set(proc::InSend);
    sock->add_waiter(p);

    log(ipc, "send: %s blocked on send on %d (%p -> %p)\n", p->name(), fd, sock.get(), other_side.get());
    getcpu().run();
}

NORETURN void syscall_pulse(Process *p, int fd, u64 bits) {
    log(pulse, "%s sending pulse %lx to %d\n", p->name(), bits, fd);

    auto sock = p->aspace->get_socket(fd);
    check_error(sock, p, -EBADF);

    const RefCnt<Socket> other_side(sock->other_side); // may be weak ref, convert to strong
    // auto [rcpt,fd] = get_recipient?
    if (auto recipient = other_side->get_recipient()) {
        recipient->blocked_socket = nullptr;

        int fd = msg_dest_fd(recipient->recv_dest);
        if (fd < 0)
            fd = recipient->aspace->get_file_number(other_side);
        const uintptr_t send_bits = other_side->get_reset_event_bits() | bits;
        log(pulse, "delivering %lx to %d (%s)\n", send_bits, fd, recipient->name());
        // transfer_pulse always switches to target process as it was woken up
        p->regs.rax = 0;
        getcpu().queue(p);
        transfer_pulse(recipient, p, fd, send_bits);
    }

    log(pulse, "%s: pulse not deliverable, saved\n", p->name());
    other_side->add_event_bits(bits);
    syscall_return(p, 0);
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
    uintptr_t offsetFlags;
    int fd;
    if (!p->aspace->find_mapping(vaddr, offsetFlags, fd)) {
        // TODO Error code instead
        abort("No mapping for PFAULT");
    }

    log(prefault, "%s prefault: mapped to %d offset %lx\n", p->name(), fd, offsetFlags);
    // TODO Should rather be a transaction flag.
    p->set(proc::PFault);
    ipc_call(p, SYS_PFAULT, fd, offsetFlags & -4096, flags & offsetFlags);
}

// Respond to a PFAULT message from a process that's mapped some memory from
// us. This could be from a "prefault" syscall, or from the page fault
// exception handler.
NORETURN void syscall_grant(Process *p, u64 dest, uintptr_t vaddr, uintptr_t flags) {
    using namespace aspace;

    // TODO Error out instead of adjusting
    flags &= MAP_RWX;

    const int fd = msg_dest_fd(dest);
    auto sock = p->aspace->get_socket(fd);
    check_error(sock, p, -EBADF);
    log(grant, "%s grant(fd=%d vaddr=%#lx flags=%lu)\n", p->name(), fd, vaddr, flags);

    // Find faulted process in otherspace
    auto rcpt = sock->get_recipient();
    if (!rcpt) {
        abort("No-one seems to be waiting for a page fault...\n");
    }
    // TODO The pfault check should move up into get_recipient() so we can get the right one. (also, should be a transaction flag so this is just end_transaction() and a check for match)
    if (!rcpt->is(proc::PFault)) {
        // Note that rcpt has already been popped from a waiting list, to
        // allow more progress we should add it back if we don't send anything.
        abort("GRANT target is not handling a fault\n");
    }
    auto fault_addr = rcpt->fault_addr;
    uintptr_t offsetFlags;
    int mapped_fd;
    if (!rcpt->aspace->find_mapping(fault_addr, offsetFlags, mapped_fd)) {
        abort("Process fault addr is not mapped\n");
    }
    if (rcpt->aspace->get_socket(mapped_fd).get() != sock->other_side) {
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
    rcpt->aspace->add_shared_backing(fault_addr | flags, sharing);
    // TODO Add the PTE for the newly added page to save a page fault

    rcpt->unset(proc::PFault);
    if (rcpt->is(proc::InRecv)) {
        // If this is set we got here from an explicit pfault syscall, so
        // respond with a message.
        transfer_message(rcpt, p, msg_send(SYS_GRANT), dest, vaddr, flags);
    }
    unimpl("grant after page fault");
}

NORETURN void syscall_yield(Process *p) {
    auto &cpu = getcpu();
    cpu.queue(p);
    cpu.run();
}

NORETURN void syscall_socketpair(Process *p) {
    RefCnt<Socket> server = nullptr;
    RefCnt<Socket> client = nullptr;
    Socket::pair(server, client);
    server->owner = p;
    client->owner = p;
    int serverfd = p->aspace->add_file(std::move(server));
    int clientfd = p->aspace->add_file(std::move(client));
    log(socket, "%s: socketpair -> s=%d c=%d\n", p->name(), serverfd, clientfd);
    syscall_return(p, 0, serverfd, clientfd);
}

NORETURN void syscall_close(Process *p, int fd) {
    log(socket, "%s: close(%d)\n", p->name(), fd);
    p->aspace->replace_file(fd, nullptr);
    syscall_return(p, 0);
}

// non-transaction send and receive still need a 

} // namespace

extern "C" void syscall(u64, u64, u64, u64, u64, u64, u64) NORETURN;

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
    case SYS_HMOD:
        syscall_hmod(p, arg0, arg1, arg2);
        break;
    case SYS_WRITE:
        Console::write(arg0, true);
        syscall_return(p, 0);
        break;
    case SYS_IO:
        syscall_portio(p, arg0, arg1, arg2);
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
    case SYS_SOCKETPAIR:
        syscall_socketpair(p);
        break;
    case SYS_CLOSE:
        syscall_close(p, arg0);
        break;
    default:
        if (nr >= MSG_USER) {
            if (msg_get_kind(nr) == MSG_KIND_SEND) {
                ipc_send(p, nr, arg0, arg1, arg2, arg3, arg4, arg5);
            } else if (msg_get_kind(nr) == MSG_KIND_CALL) {
                ipc_call(p, nr, arg0, arg1, arg2, arg3, arg4, arg5);
            }
        }
        printf("unimplemented syscall: %ld\n", nr);
        syscall_return(p, -ENOSYS);
    }
}

} // system
