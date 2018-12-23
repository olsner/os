#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

#include <memory>
template <typename T> using RefCnt = std::shared_ptr<T>;
template <typename T> using Ptr = std::unique_ptr<T>;

#include "types.h"

#define PACKED __attribute__((packed))
#define UNUSED __attribute__((unused))
#define NORETURN __attribute__((noreturn))
#define NOINLINE __attribute__((noinline))
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))

extern "C" void start64() NORETURN;

extern "C" void printf(const char* fmt, ...);

void unimpl(const char* what) NORETURN;

void strlcpy(char *dst, const char *src, size_t dstsize) {
    size_t n = std::strlen(src);
    if (n > dstsize - 1) n = dstsize = 1;
    std::memcpy(dst, src, n);
    dst[n] = 0;
}

#define S_(X) #X
#define S(X) S_(X)
#define log_fileline 1

#define log_idle 0
#define log_switch 0
#define log_runqueue 0
#define log_page_fault 0
#define log_add_pte 0
#define log_int_entry 0
#define log_int_entry_regs 0
#define log_irq 1
#define log_portio 0
#define log_hmod 0
#define log_dict_find 0
#define log_dict_insert 0
#define log_ipc 0
#define log_transfer_message 0
#define log_assoc_procs 0
#define log_recv 0
#define log_map_range 0
#define log_syscall 0
#define log_prefault 0
#define log_grant 0
#define log_waiters 0
#define log_pulse 0

#define log(scope, fmt, ...) do { \
    if (log_ ## scope) { \
        if (log_fileline) { \
            printf("K[%s] %s:%d: " fmt, #scope, __FILE__, __LINE__, ## __VA_ARGS__); \
        } else { \
            printf(fmt, ## __VA_ARGS__); \
        } \
    } \
} while (0)

#define XPRINTF_NOERRNO
#define XPRINTF_LINKAGE extern "C" UNUSED
#define xprintf printf
#include "xprintf.cpp"

#include "addr.h"
#include "mboot.h"
#include "mem.h"
#include "x86.h"

using std::abort;
void NORETURN abort(const char *msg) {
    puts(msg);
    abort();
}

void unimpl(const char *what) {
    printf("UNIMPL: %s\n", what);
    abort();
}

using std::latch;

namespace {

// Symbols exported by start32.o
namespace start32 {
    extern "C" u32 memory_start;

    namespace low {
        extern "C" u32 mbi_pointer;
        extern "C" x86::gdtr gdtr;
        extern "C" u64 kernel_pdp[512];
    }

    static const x86::gdtr& gdtr = *HighAddr(&low::gdtr);
    static const mboot::Info& mboot_info() {
        return *PhysAddr<mboot::Info>(low::mbi_pointer);
    }
    static const uintptr_t kernel_pdp_addr = (uintptr_t)&low::kernel_pdp;
}

void dumpMBInfo(const mboot::Info& info) {
    printf("Multiboot info at %p\n", &info);

    printf("Flags: %x\n", info.flags);
    if (info.has(mboot::MemorySize)) {
        printf("%ukB lower memory, %ukB upper memory, %uMB total\n",
            info.mem_lower, info.mem_upper,
            (info.mem_lower + info.mem_upper + 1023) / 1024);
    }
    if (info.has(mboot::CommandLine)) {
        const char* cmdline = PhysAddr<char>(info.cmdline);
        printf("Command line @%p (%x) \"%s\"\n", cmdline, info.cmdline, cmdline);
    }
}

namespace proc { struct Process; }
using proc::Process;
namespace aspace { struct AddressSpace; }
using aspace::AddressSpace;

#include "dict.h"
#include "dlist.h"
#include "reflist.h"
#include "handle.h"
#include "aspace.h"
#include "proc.h"
#include "cpu.h"
using cpu::Cpu;
using cpu::getcpu;
#include "syscall.h"

RefCnt<Process> new_proc_simple(u32 start, u32 end_unaligned, const char *name) {
    u32 end = (end_unaligned + 0xfff) & ~0xfff;
    u32 start_page = start & ~0xfff;
    auto aspace = std::make_shared<AddressSpace>();
    aspace->set_name(name);
    using namespace aspace; // for MAP_*
    aspace->mapcard_set(0x0ff000, 0, 0, MAP_ANON | MAP_RW);
    aspace->mapcard_set(0x100000, 0, start_page - 0x100000, MAP_PHYS | MAP_RX);
    aspace->mapcard_set(0x100000 + (end - start_page), 0, 0, 0);

    auto ret = std::make_shared<Process>(std::move(aspace));
    ret->regs.rsp = 0x100000;
    ret->rip = 0x100000 + (start & 0xfff);
    return ret;
}

void assoc_procs(RefCnt<Process> p, uintptr_t i, RefCnt<Process> q, uintptr_t j) {
    log(assoc_procs, "%p:%lu <-> %lu:%p\n", p.get(), i, j, q.get());
    p->assoc_handles(j, q, i);
}

void init_modules(Cpu *cpu, const mboot::Info& info) {
    assert(info.has(mboot::Modules));
    auto mod = PhysAddr<mboot::Module>(info.mods_addr);
    const size_t count = info.mods_count;
    printf("%zu module(s)\n", count);
    RefCnt<Process> *procs = new RefCnt<Process>[count];
    for (size_t n = 0; n < count; n++) {
        const char *name = PhysAddr<char>(mod->string);
        printf("Module %#x..%#x: %s\n", mod->start, mod->end, name);
        procs[n] = new_proc_simple(mod->start, mod->end, name);
        mod++;
    }
    if (count) {
        cpu->irq_process = procs[0];
    }
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            assoc_procs(procs[i], i + 1, procs[j], j + 1);
        }
    }
    for (size_t i = 0; i < count; i++) {
        cpu->queue(std::move(procs[i]));
    }
    delete[] procs;
}

namespace pf {
    enum Errors {
        Present = 1,
        Write = 2,
        User = 4,
        Reserved = 8,
        Instr = 16,
    };
};

NOINLINE NORETURN void page_fault(RefCnt<Process>& p, u64 error) {
    log(page_fault, "page fault %lx cr2=%p rip=%p in %s\n",
        error, (void*)x86::cr2(), (void*)p->rip, p->name());

    assert(error & pf::User);
    i64 fault_addr = x86::cr2();
    assert(fault_addr >= 0);

    auto& as = p->aspace;
    auto *back = as->find_add_backing(fault_addr & -0x1000);
    if (!back) {
        printf("Fatal page fault in %s. err=%lx cr2=%p\n", p->name(), error, (void*)x86::cr2());
        p->dump_regs();
        abort();
    }
    as->add_pte(back->vaddr(), back->pte());

    getcpu().switch_to(p);
}

void handle_irq_generic(Cpu *cpu, u8 vec) {
    auto p = cpu->irq_process;
    assert(p);
    log(irq, "IRQ %d triggered, irq process is %s (%zu)\n", vec, p->name(), p.use_count());

    vec -= 32;
    u8 ix = vec >> 6;
    u64 mask = 1 << (vec & 63);
    if (cpu->irq_delayed[ix] & mask) {
        // Already delayed, so we can't do anything else here
        log(irq, "handle_irq_generic: already delayed\n");
        return;
    }
    cpu->irq_delayed[ix] |= mask;

    if (auto rcpt = p->aspace->pop_open_recipient()) {
        auto irqs = latch(cpu->irq_delayed[0]);
        log(irq, "handle_irq_generic: sending %lx to %s\n", irqs, rcpt->name());
        syscall::transfer_pulse(rcpt, 0, irqs);
    }
}

} // namespace

extern "C" void int_entry(u8 vec, u64 err, Cpu *cpu) NORETURN;

void int_entry(u8 vec, u64 err, Cpu *cpu) {
    log(int_entry, "int_entry(%u, %#lx, cr2=%#lx) in %s\n", vec, err, x86::cr2(), cpu->process ? cpu->process->name() : "(idle)");
    assert(cpu == &getcpu());
    if (vec == 8 || (vec == 14 && !(err & pf::User))) {
        if (vec == 8) {
            printf("Double fault!\n");
        } else {
            printf("Kernel page fault! %#lx, cr2=%#lx\n", err, x86::cr2());
        }
        cpu->dump_regs();
        asm("cli;hlt");
        __builtin_unreachable();
    }
    RefCnt<Process> p = cpu->process;
    if (p) {
        if (log_int_entry_regs) {
            printf("Process registers (%s)\n", cpu->process->name());
            cpu->process->saved_regs.dump();
        }
        printf("Leaving process with %zu refs\n", p.use_count());
        cpu->leave(p);
    } else {
        log(irq, "Got interrupt %u while idle\n", vec);
    }
    // TODO Add symbolic constants for all defined exceptions
    switch (vec) {
    case 14:
        assert(p);
        page_fault(p, err);
        break;
    default:
        if (vec >= 32) {
            if (p) {
                cpu->queue(std::move(p));
            }
            handle_irq_generic(cpu, vec);
            cpu->run();
        } else {
            printf("Unimplemented CPU Exception #%d\n", vec);
            abort();
        }
    }
}

namespace {
typedef void (*Ctor)();
extern "C" Ctor __CTOR_LIST__[];
extern "C" Ctor __CTOR_END__[];

void run_constructors(Ctor *p, Ctor *end) {
    while (p != end) (*p++)();
}
}

void start64() {
    run_constructors(__CTOR_LIST__, __CTOR_END__);
    dumpMBInfo(start32::mboot_info());

    x86::lgdt(start32::gdtr);
    x86::ltr(x86::seg::tss64);
    idt::init();

    mem::init(start32::mboot_info(), start32::memory_start, -kernel_base);
//  write("Memory initialized. ");
//  mem::stat();

    auto cpu = new Cpu();
    cpu->start();
    init_modules(cpu, start32::mboot_info());
    cpu->run();
}
