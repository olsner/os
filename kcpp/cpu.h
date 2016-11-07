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

    static const size_t GDT_SIZE = 88;
    static const size_t TSS_SIZE = 0x68;

    uint8_t gdt[GDT_SIZE];
    uint8_t tss[TSS_SIZE];

    // Assume everything else is 0-initialized
    Cpu(): self(this), stack(new u8[4096] + 4096) {
        assert(GDT_SIZE == start32::gdt_end - start32::gdt_start);
        memcpy(gdt, start32::gdt_start, GDT_SIZE);

        // IST0 with per-cpu stack
        write_u64(tss + 4, (u64)stack);
        // Start of I/O bitmap
        write_u16(tss + 0x66, 0x68);

        // TSS64 descriptor format:
        //	0		8	16		24
        // +0 	limit (16)		addr (0:16)
        // +4	addr (16:24)	flags/access		addr (24:32)
        // +8	addr (32:64)
        // +12	0 (32 bits)
        u64 tss_addr = (u64)tss;
        u8 *desc = gdt + x86::seg::tss64;
        write_u32(desc + 8, tss_addr >> 32);
        write_u16(desc + 2, tss_addr);
        write_u8(desc + 4, tss_addr >> 16);
        write_u8(desc + 7, tss_addr >> 24);
        // The limit is prefilled in the GDT from start32.inc.
    }

    // Runs on the CPU itself to set up CPU state.
    void start() {
        x86::lgdt(x86::gdtr { GDT_SIZE - 1, (u64)gdt });
        x86::ltr(x86::seg::tss64);
        idt::load();
        setup_msrs((u64)this);
    }

    NORETURN void run() {
        assert(this == self);
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
        assert(this == self);
        if (p->is(proc::FastRet)) {
            p->unset(proc::FastRet);
            fastret(p, p->regs.rax);
        } else {
            slowret(p);
        }
    }

    NORETURN void syscall_return(Process *p, u64 rax) {
        assert(this == self);
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
