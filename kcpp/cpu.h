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

struct KernelRegSave {
    x86::Regs regs;
    u64 rip;
    u64 rflags;
    // syscall.asm does have cr3 here too, but that's not saved in interrupts
    // since the value in Proc is authoritative anyway.
    // u64 cr3;

    void dump() {
        printf("RIP= %16lx  RFLAGS= %lx\n", rip, rflags);
#define R(n) #n "= %16lx  "
#define N "\n"
        // Left column is not the correct number order
        // (should be a,c,d,b, sp,bp, si,di)
        printf(
            R(rax) " " R(r8) N
            R(rcx) " " R(r9) N
            R(rdx) R(r10) N
            R(rbx) R(r11) N
            R(rsp) R(r12) N
            R(rbp) R(r13) N
            R(rsi) R(r14) N
            R(rdi) R(r15) N,
#undef R
#define R(r) regs.r
            R(rax), R(r8), R(rcx), R(r9), R(rdx), R(r10), R(rbx), R(r11),
            R(rsp), R(r12), R(rbp), R(r13), R(rsi), R(r14), R(rdi), R(r15));
    }
};

struct Cpu {
    // NB: Initial fields shared with assembly code.
    Cpu *self;
    u8 *stack;
    Process *process;
    KernelRegSave *kernel_reg_save_pointer;
    // END OF ASSEMBLY-SHARED FIELDS

    // mem::PerCpu memory
    DList<Process> runqueue;
    Process *irq_process;
    Process *fpu_process;

    KernelRegSave kernel_reg_save;

    // Assume everything else is 0-initialized
    // FIXME There's already a stack allocated by the boot loader, a bit
    // wasteful to allocate a new one. Non-first CPUs might need this code
    // though?
    Cpu():
        self(this),
        stack(new u8[4096]),
        kernel_reg_save_pointer(&kernel_reg_save) {
    }

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
        log(runqueue, "queue %s. queued=%d flags=%lu\n", p->name(), p->is_queued(), p->flags);
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

    // TODO Using fastret here should be guaranteed possible, so we can avoid
    // going through memory for rax. Note that we don't always return to the
    // same process that called (e.g. in IPC cases when the old is blocked and
    // the new is immediately made runnable), so the context switch stuff in
    // switch_to is mostly still necessary.
    NORETURN void syscall_return(Process *p, u64 rax) {
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
