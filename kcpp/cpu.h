namespace cpu {

namespace {
extern "C" void fastret(Process *p, u64 rax) NORETURN;
extern "C" void slowret(Process *p) NORETURN;
extern "C" void syscall_entry_stub();
extern "C" void syscall_entry_compat();
}

struct Cpu;
void idle(Cpu *) NORETURN;

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

struct Cpu {
    Cpu *self;
    u8 *stack;
    Process *process;
    // mem::PerCpu memory
    DList<Process> runqueue;
    Process *irq_process;
    Process *fpu_process;

    // Assume everything else is 0-initialized
    // FIXME There's already a stack allocated by the boot loader, a bit
    // wasteful to allocate a new one. Non-first CPUs might need this code
    // though?
    Cpu(): self(this), stack(new u8[4096]) {
    }

    void start() {
        setup_msrs((u64)this);
    }

    NORETURN void run() {
        if (Process *p = runqueue.pop()) {
            assert(p->is_queued());
            p->unset(proc::Queued);
            switch_to(p);
        } else {
            idle(this);
        }
    }

    void queue(Process *p) {
        log(runqueue, "queue %p. queued=%d flags=%lu\n", p, p->is_queued(), p->flags);
        assert(p->is_runnable());
        if (!p->is_queued()) {
            p->set(proc::Queued);
            runqueue.append(p);
        }
    }

    void leave(Process *p) {
        assert(p == process);
        p->unset(proc::Running);
        process = NULL;
    }

    NORETURN void switch_to(Process *p) {
        log(switch, "switch_to %p rip=%#lx fastret=%d queued=%d\n",
                p, p->rip, p->is(proc::FastRet), p->is(proc::Queued));
        assert(!process);
        assert(!p->is(proc::Running));
        assert(p->is_runnable());
        p->set(proc::Running);
        if (process != p) {
            process = p;
            // TODO check fpu_process too (clear the task switch flag so it
            // won't fault again)
            x86::set_cr3(p->cr3);
        }
        if (p->is(proc::FastRet)) {
            p->unset(proc::FastRet);
            fastret(p, p->regs.rax);
        } else {
            slowret(p);
        }
    }

    NORETURN void syscall_return(Process *p, u64 rax) {
        p->regs.rax = rax;
        switch_to(p);
    }
};

void idle(Cpu *cpu) {
    for (;;) {
        log(idle, "idle\n");
        cpu->process = NULL;
        asm volatile("cli; hlt; cli" ::: "memory");
    }
}

Cpu &getcpu() {
    return *(Cpu *)x86::get_cpu_specific();
}

}
