namespace cpu {

extern "C" void fastret(Process *p, u64 rax) NORETURN;
extern "C" void slowret(Process *p) NORETURN;

struct Cpu {
    Cpu *self;
    u8 *stack;
    Process *process;
    // mem::PerCpu memory
    // DList<Process> runqueue
    Process *irq_process;

    void start() {
    }

    NORETURN void run() {
        unimpl("run");
    }

    void queue(Process *p) {
        (void)p;
        unimpl("queue");
    }
};

}
