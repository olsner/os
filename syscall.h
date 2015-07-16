namespace syscall {

extern "C" void syscall(u64, u64, u64, u64, u64, u64, u64) NORETURN;

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

namespace {
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
};

void syscall(u64 arg0, u64 arg1, u64 arg2, u64 arg5, u64 arg3, u64 arg4, u64 nr) {
    printf("syscall %#x: %lx %lx %lx %lx %lx %lx\n", (unsigned)nr, arg0, arg1, arg2, arg3, arg4, arg5);
    auto p = getcpu().process;
    p->unset(proc::Running);
    p->set(proc::FastRet);

    p->regs.rax = 0;
    switch (nr) {
    case SYS_RECV:
        //unimpl("recv");
        break;
    case SYS_MAP:
        unimpl("map");
        break;
    case SYS_HMOD:
        hmod(p, arg0, arg1, arg2);
        break;
    case SYS_IO:
        p->regs.rax = portio(arg0, arg1, arg2);
        break;
    default:
        if (nr >= MSG_USER) {
            unimpl("ipc");
        } else {
            abort("unhandled syscall");
        }
    }

    // Should the return path go back to p or to the next process by default?
    getcpu().queue(p);
    getcpu().run();
}

}
