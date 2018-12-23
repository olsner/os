namespace cpu {

namespace {
extern "C" void fastret(Process *p, u64 rax) NORETURN;
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

struct CpuAsm {
    CpuAsm *self;
    u8 *stack;
    // TODO Remove the process_ptr and don't use it from asm.
    Process* process_ptr;
    SavedRegs *kernel_reg_save_pointer;

    // Assume everything else is 0-initialized
    // FIXME There's already a stack allocated by the boot loader, a bit
    // wasteful to allocate a new one. Non-first CPUs might need this code
    // though?
    CpuAsm(SavedRegs *kernel_reg_save):
        self(this),
        stack(new u8[4096]),
        kernel_reg_save_pointer(kernel_reg_save) {
    }
};

struct Cpu: CpuAsm {
    RefList<Process> runqueue;
    RefCnt<Process> irq_process;
    RefCnt<Process> process;
    u64 irq_delayed[4];

    SavedRegs kernel_reg_save;

    Cpu(): CpuAsm(&kernel_reg_save) {}
    Cpu(Cpu&) = delete;
    Cpu& operator=(Cpu&) = delete;

    void start() {
        setup_msrs((u64)this);
    }

    NORETURN void run() {
        if (RefCnt<Process> p = runqueue.pop()) {
            log(switch, "run: popped %s\n", p->name());
            assert(p->is_queued());
            p->unset(proc::Queued);
            switch_to(p);
        } else {
            idle(this);
        }
    }

    void queue(RefCnt<Process> p) {
        log(runqueue, "queue %s. queued=%d flags=%lu\n", p->name(), p->is_queued(), p->flags);
        assert(p->is_runnable());
        if (!p->is_queued()) {
            p->set(proc::Queued);
            runqueue.append(p);
        }
    }

    void leave(RefCnt<Process> p) {
        assert(process == p);
        log(runqueue, "leaving %s (%zu)\n", p->name(), p.use_count());
        p->unset(proc::Running);
        process.reset();
        process_ptr = nullptr;
        log(runqueue, "left %s (%zu)\n", p->name(), p.use_count());
    }

    NORETURN void switch_to(RefCnt<Process>& p) {
        log(switch, "switch_to %s(%zu) rip=%#lx fastret=%d queued=%d\n",
                p->name(), p.use_count(), p->rip, p->is(proc::FastRet), p->is(proc::Queued));
        assert(this == &getcpu());
        assert(!process);
        assert(!p->is(proc::Running));
        assert(p->is_runnable());
        p->set(proc::Running);
        if (process != p) {
            process = std::move(p);
            x86::set_cr3(process->cr3);
        } else {
            p.reset();
        }
        process_ptr = process.get();
        log(switch, "switch_to %s (%zu)\n", process->name(), process.use_count());
        if (process->is(proc::FastRet)) {
            process->unset(proc::FastRet);
            fastret(process.get(), process->regs.rax);
        } else {
            slowret(process.get());
        }
    }

    // TODO Using fastret here should be guaranteed possible, so we can avoid
    // going through memory for rax. Note that we don't always return to the
    // same process that called (e.g. in IPC cases when the old is blocked and
    // the new is immediately made runnable), so the context switch stuff in
    // switch_to is mostly still necessary.
    NORETURN void syscall_return(RefCnt<Process>& p, u64 rax) {
        log(switch, "syscall_return %s rax=%lx\n", p->name(), p->regs.rax);
        p->regs.rax = rax;
        switch_to(p);
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
    cpu->process = nullptr;
    cpu->process_ptr = nullptr;
    asm volatile("sti; hlt" ::: "memory");
    // We should have entered an interrupt handler which would not "return"
    // here but rather just re-idle.
    abort("idle returned");
}

}
