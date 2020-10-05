namespace cpu {

namespace {
extern "C" void fastret(u64 arg1, u64 arg2, u64 arg3, Process *p, u64 arg4, u64 arg5, u64 arg6, u64 rax) NORETURN;
extern "C" void slowret(Process *p) NORETURN;
extern "C" void syscall_entry_stub();
extern "C" void syscall_entry_compat();
}

struct Cpu;
void idle(Cpu *) NORETURN;

Cpu &getcpu() {
    return *(Cpu *)x86::get_cpu_specific();
}

void setup_msrs(u64 gs) {
    using x86::seg;
    using x86::rflags;
    using x86::efer;
    using namespace x86::msr;

    wrmsr(STAR, (seg::user_code32_base << 16) | seg::code);
    wrmsr(LSTAR, (uintptr_t)syscall_entry_stub);
    wrmsr(CSTAR, (uintptr_t)syscall_entry_compat);
    // FIXME: We want to clear a lot more flags - Direction for instance.
    // FreeBSD sets PSL_NT|PSL_T|PSL_I|PSL_C|PSL_D
    wrmsr(FMASK, rflags::IF | rflags::VM);
    wrmsr(EFER, rdmsr(EFER) | efer::SCE | efer::NXE);
    wrmsr(GSBASE, gs);
}

using x86::SavedRegs;

struct Cpu {
    // NB: Initial fields shared with assembly code.
    Cpu *self;
    u8 *stack;
    Process *process;
    SavedRegs *kernel_reg_save_pointer;
    // END OF ASSEMBLY-SHARED FIELDS

    // mem::PerCpu memory
    DList<Process> runqueue;
    Process *irq_process;
    u64 irq_delayed[4];
    Process *last_process;

    SavedRegs kernel_reg_save;

    // Assume everything else is 0-initialized
    // FIXME There's already a stack allocated by the boot loader, a bit
    // wasteful to allocate a new one. Non-first CPUs might need this code
    // though?
    Cpu():
        self(this),
        stack(new u8[4096]),
        kernel_reg_save_pointer(&kernel_reg_save) {
    }
    Cpu(Cpu&) = delete;
    Cpu& operator=(Cpu&) = delete;

    void start() {
        setup_msrs((u64)this);
    }

    NORETURN void run() {
        if (Process *p = runqueue.pop()) {
            log(switch, "run: popped %s\n", p->name());
            assert(p->is_queued());
            p->unset(proc::Queued);
            switch_to(p);
        } else {
            idle(this);
        }
    }

    void queue(Process *p) {
        log(runqueue, "queue %s. queued=%d flags=%u\n", p->name(), p->is_queued(), p->flags);
        assert(p->is_runnable());
        if (!p->is_queued()) {
            p->set(proc::Queued);
            runqueue.append(p);
        }
    }

    void leave(Process *p) {
        assert(p == process);
        log(runqueue, "leaving %s\n", p->name());
        p->unset(proc::Running);
        process = NULL;
    }

    NORETURN void switch_to(Process *p) {
        log(switch, "switch_to %s rip=%#lx fastret=%d queued=%d\n",
                p->name(), p->rip, p->is(proc::FastRet), p->is(proc::Queued));
        assert(this == &getcpu());
        assert(!process);
        assert(!p->is(proc::Running));
        assert(p->is_runnable());
        p->set(proc::Running);
        if (process != p) {
            process = p;
            last_process = p;
            x86::set_cr3(p->cr3());
        }
        if (p->is(proc::FastRet)) {
            p->unset(proc::FastRet);
            fastret(p->regs.rdi, p->regs.rsi, p->regs.rdx, p, p->regs.r8, p->regs.r9, p->regs.r10, p->regs.rax);
        } else {
            slowret(p);
        }
    }

    // TODO Using fastret here should be guaranteed possible, so we can avoid
    // going through memory for rax. Note that we don't always return to the
    // same process that called (e.g. in IPC cases when the old is blocked and
    // the new is immediately made runnable), so the context switch stuff in
    // switch_to is mostly still necessary.
    NORETURN void syscall_return(Process *p, u64 rax, u64 arg1 = 0, u64 arg2 = 0, u64 arg3 = 0, u64 arg4 = 0, u64 arg5 = 0, u64 arg6 = 0) {
        log(switch, "syscall_return %s rax=%lx arg1=%lx arg2=%lx\n", p->name(), rax, arg1, arg2);
        assert(p->is(proc::FastRet));
        if (p->is(proc::FastRet)) {
            p->set(proc::Running);
            if (process != p) {
                process = p;
                last_process = p;
                x86::set_cr3(p->cr3());
            }
            p->unset(proc::FastRet);
            fastret(arg1, arg2, arg3, p, arg4, arg5, arg6, rax);
        }
        else {
            p->regs.rax = rax;
            p->regs.rdi = arg1;
            p->regs.rsi = arg2;
            p->regs.rdx = arg3;
            switch_to(p);
        }
    }

    void dump_stack(u64 *start, u64 *end) {
        while (start < end) {
            printf("%p: %16lx\n", start, *start);
            start++;
        }
    }

    void dump_regs() {
        kernel_reg_save.dump();
        u64* sp = (u64*)kernel_reg_save.regs.rsp;
        dump_stack(sp, (u64*)(((uintptr_t)sp + 0xfff) & ~0xfff));
    }
};

void idle(Cpu *cpu) {
    log(idle, "idle\n");
    cpu->process = NULL;
    asm volatile("sti; hlt" ::: "memory");
    // We should have entered an interrupt handler which would not "return"
    // here but rather just re-idle.
    abort("idle returned");
}

}
