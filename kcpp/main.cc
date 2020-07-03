#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uintptr_t size_t;
typedef intptr_t ssize_t;
typedef unsigned int uint;

#define PACKED __attribute__((packed))
#define UNUSED __attribute__((unused))
#define NORETURN __attribute__((noreturn))
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))

extern "C" void start64() NORETURN;

#define STRING_INL_LINKAGE static UNUSED
#include "string.c"

namespace {

void assert_failed(const char* file, int line, const char* msg) NORETURN UNUSED;
extern "C" void abort() NORETURN;
void abort(const char *msg) NORETURN;
void unimpl(const char* what) NORETURN;

void strlcpy(char *dst, const char *src, size_t dstsize) {
    size_t n = strlen(src);
    if (n > dstsize - 1) n = dstsize = 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

}

#define S_(X) #X
#define S(X) S_(X)
#define assert(X) \
    do { if (enable_assert && !(X)) { assert_failed(__FILE__, __LINE__, #X); } } while (0)

#define enable_assert 1
#define log_fileline 1

// Routing of user and kernel console messages to debug/VGA console
#define user_debugcon 1
#define user_vgacon 1
#define kernel_debugcon 1
#define kernel_vgacon 1

#define log_idle 0
#define log_switch 0
#define log_runqueue 0
#define log_page_fault 0
#define log_add_pte 0
#define log_int_entry 0
#define log_int_entry_regs 0
#define log_irq 0
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
#define log_socket 1
#define log_files 1

#define log(scope, fmt, ...) do { \
    if (log_ ## scope) { \
        if (log_fileline) { \
            printf("K[%s] %s:%d: " fmt, #scope, __FILE__, __LINE__, ## __VA_ARGS__); \
        } else { \
            printf(fmt, ## __VA_ARGS__); \
        } \
    } \
} while (0)

namespace { namespace mem { void *malloc(size_t); void free(void *); } }

using mem::malloc;
using mem::free;

void *operator new(size_t sz) {
    return malloc(sz);
}
void operator delete(void *p) {
    free(p);
}
void operator delete(void *p, size_t) {
    free(p);
}
void *operator new[](size_t sz) {
    return malloc(sz);
}
void operator delete[](void *p) {
    free(p);
}
void operator delete[](void *p, size_t) {
    free(p);
}
void* operator new(size_t, void* p) {
    return p;
}

namespace {

namespace Console {
    void write(char c, bool fromUser);
}

// Might actually want to specialize xprintf for the kernel...
struct FILE {};
static FILE* const stdout = NULL;
static FILE* const stderr = NULL;
void flockfile(FILE *) {}
void funlockfile(FILE *) {}
void fflush(FILE *) {}
ssize_t fwrite_unlocked(const void* p, size_t sz, size_t n, FILE *) {
    size_t c = n * sz;
    const char *str = (char*)p;
    while (c--) Console::write(*str++, false);
    return n;
}
void fputc_unlocked(char c, FILE *) {
    Console::write(c, false);
}
long int strtol(const char *p, char **end, int radix);
bool isdigit(char c) {
    return c >= '0' && c <= '9';
}

#define XPRINTF_NOSTDLIB
#define XPRINTF_NOERRNO
#define XPRINTF_LINKAGE static UNUSED
#include "xprintf.cpp"

#define printf xprintf

long int strtol(const char *p, char **end, int radix) {
    assert(radix == 10);
    if (!memcmp(p, "16l", 3)) {
        if (end) *end = (char*)p + 2;
        return 16;
    }
    printf("strtol(%s,%p,%d)\n", p, end, radix);
    unimpl("strtol");
}
static const intptr_t kernel_base = -(1 << 30);

template <class T>
static constexpr T* PhysAddr(uintptr_t phys) {
    return (T*)(phys + kernel_base);
}
template <class T>
static constexpr T* HighAddr(T* lowptr) {
    return PhysAddr<T>((uintptr_t)lowptr);
}
uintptr_t ToPhysAddr(const volatile void *p) {
    return (uintptr_t)p - kernel_base;
}

static void memset16(u16* dest, u16 value, size_t n) {
    if (/* constant(value) && */ (value >> 8) == (value & 0xff)) {
        memset(dest, value, n * 2);
    } else {
        // rep movsw
        while (n--) *dest++ = value;
    }
}

namespace Console {
    static u16* const buffer = PhysAddr<u16>(0xb80a0);
    static u16 pos;
    static const u8 width = 80, height = 24;

    UNUSED void clear() {
        pos = 0;
        memset16(buffer, 0, width * height);
    }

    void debugcon_putc(char c) {
        asm("outb %0,%1"::"a"(c),"d"((u16)0xe9));
    }

    void write(char c, bool fromUser) {
        if (fromUser ? user_debugcon : kernel_debugcon) {
            debugcon_putc(c);
        }
        if (fromUser ? user_vgacon : kernel_vgacon) {
            if (c == '\n') {
                u8 fill = width - (pos % width);
                memset16(buffer + pos, 0, fill);
                pos += fill;
            } else {
                buffer[pos++] = 0x0700 | c;
            }
            if (pos == width * height) {
                memmove(buffer, buffer + width, sizeof(*buffer) * width * (height - 1));
                pos -= width;
                memset16(buffer + pos, 0, width);
            }
        }
    }

    void write(const char *s, bool fromUser = false) {
        while (char c = *s++) write(c, fromUser);
    }
};

using Console::write;

void unimpl(const char *what) {
    printf("UNIMPL: %s\n", what);
    abort();
}

void abort(const char *msg) {
    write(msg);
    abort();
}

void abort() {
    asm("cli;hlt");
    __builtin_unreachable();
}

template <typename T, typename U = T>
T latch(T& var, U value = U()) {
    T res = var;
    var = value;
    return res;
}

namespace x86 {
    namespace msr {
        enum MSR {
            EFER = 0xc0000080,
            STAR = 0xc0000081,
            LSTAR = 0xc0000082,
            CSTAR = 0xc0000083,
            FMASK = 0xc0000084,
            GSBASE = 0xc0000101
        };

        void wrmsr(MSR msr, u64 val) {
            asm volatile("wrmsr"
                :
                : "c"(msr),
                  "d"(val >> 32),
                  "a"(val)
                : "memory");
        }
        u64 rdmsr(MSR msr) {
            u64 l, h;
            asm("rdmsr": "=d"(h), "=a"(l) : "c"(msr));
            return h << 32 | l;
        }
    };
    enum rflags : u64 {
        IF = 1 << 9,
        VM = 1 << 17,
    };
    enum efer : u64 {
        SCE = 1 << 0,
        NXE = 1 << 11,
    };
    enum seg : u16 {
        code32 = 8,
        data32 = 16,
        code = 24,
        data = 32,
        tss64 = 40,
        user_code32_base = 56,
        user_data32_base = 64,
        user_code64_base = user_code32_base + 16,
        user_data64_base = user_code64_base + 8,
        user_cs = user_code64_base | 3,
        user_ds = user_cs + 8,
    };
    struct gdtr {
        u16 limit;
        u64 base;
    } __attribute__((packed));

    void lgdt(const gdtr& gdt) {
        asm("lgdt %0" ::"m"(gdt));
    }
    void ltr(seg tr) {
        asm("ltr %0" ::"r"(tr));
    }
    u64 cr3() {
        u64 cr3;
        asm volatile("movq %%cr3, %0" : "=r"(cr3));
        return cr3;
    }
    void set_cr3(u64 new_cr3) {
        if (new_cr3 != cr3()) {
            asm volatile("movq %0, %%cr3" :: "r"(new_cr3));
        }
    }

    u64 cr2() {
        u64 cr2;
        asm("movq %%cr2, %0" : "=r"(cr2));
        return cr2;
    }

    u64 get_cpu_specific() {
        u64 res = 0;
        asm("gs movq (%0), %0": "=r"(res) : "0"(res));
        return res;
    }

    struct Regs {
        u64 rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
        u64 r8, r9, r10, r11, r12, r13, r14, r15;
    };

    struct SavedRegs {
        Regs regs;
        u64 rip;
        u64 rflags;
        u64 cr3;

        void dump() const {
            printf("RIP=%016lx  RFLAGS=%lx\n", rip, rflags);
#define R(n) #n "=%016lx  "
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
}

namespace idt {
    using x86::seg;

    const u64 GatePresent = 0x80;
    const u64 GateTypeInterrupt = 0x0e;

    struct Entry {
        u64 low, high;

        static u64 lo(u64 addr) {
            u64 low = (addr & 0xffff) | ((u64)seg::code << 16);
            u64 flags = GatePresent | GateTypeInterrupt;
            u64 high = (addr & 0xffff0000) | (flags << 8);

            return low | (high << 32);
        }
        static u64 hi(u64 addr) {
            return addr >> 32;
        }
        void operator=(void(*fn)(void)) {
            u64 addr = (u64)fn;
            low = lo(addr);
            high = hi(addr);
        }
    };
    const size_t N_IRQ_STUBS = 224;
    const size_t IRQ_STUB_SIZE = 7;
    const size_t N_ENTRIES = 32 + N_IRQ_STUBS;
    typedef Entry Table[N_ENTRIES];

    struct idtr {
        u16 limit;
        const Table *base;
    } __attribute__((packed));

    void lidt(const idtr& idt) {
        asm("lidt %0" ::"m"(idt));
    }
    void load(const Table &table) {
        lidt(idtr { N_ENTRIES * 16 - 1, &table });
    }

    namespace {
        extern "C" void handler_PF_stub();
        extern "C" void handler_NM_stub();
        extern "C" void handler_DF_stub();
        extern "C" void asm_int_entry();
    }

    u8 *generate_idt(u8 *dst, u8 irq, void (*entry)()) {
        u8 *const pent = (u8*)entry;
        u8 *const end = dst + 7;

        // push imm8
        dst[0] = 0x6a;
        // Note: this will be sign-extended, but we can adjust for that later.
        dst[1] = irq;

        // jmp rel32
        dst[2] = 0xe9;
        // offset is relative to rip which points to the instruction following
        // the JMP instruction.
        *(int32_t *)(dst + 3) = pent - end;

        return end;
    }

    void init() {
        static Table idt_table;
        static u8 idt_code[IRQ_STUB_SIZE * N_IRQ_STUBS];
        idt_table[7] = handler_NM_stub;
        idt_table[8] = handler_DF_stub;
        idt_table[14] = handler_PF_stub;
        u8 *p = idt_code;
        for (size_t i = 32; i < N_ENTRIES; i++) {
            idt_table[i] = (void(*)())p;
            p = generate_idt(p, i, asm_int_entry);
        }
        assert(p == idt_code + sizeof(idt_code));
        load(idt_table);
    }
}

#include "mboot.h"

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
#include "mem.h"
#include "refcnt.h"
#include "handle.h"
#include "aspace.h"
#include "proc.h"
#include "cpu.h"
using cpu::Cpu;
using cpu::getcpu;
#include "syscall.h"

void assert_failed(const char* file, int line, const char* msg) {
    printf("%s:%d: ASSERT FAILED: %s\n", file, line, msg);
    Cpu& c = getcpu();
    if (c.last_process) {
        printf("Last user process: %s\n", c.last_process->name());
        // TODO Print IP and registers of user process?
    }
    abort();
}

Process *new_proc_simple(u32 start, u32 end_unaligned, const char *name) {
    u32 end = (end_unaligned + 0xfff) & ~0xfff;
    u32 start_page = start & ~0xfff;
    auto aspace = new AddressSpace();
    aspace->set_name(name);
    auto ret = new Process(aspace);
    ret->regs.rsp = 0x100000;
    ret->rip = 0x100000 + (start & 0xfff);

    using namespace aspace; // for MAP_*
    aspace->mapcard_set(0x0ff000, 0, 0, MAP_ANON | MAP_RW);
    aspace->mapcard_set(0x100000, 0, start_page - 0x100000, MAP_PHYS | MAP_RX);
    aspace->mapcard_set(0x100000 + (end - start_page), 0, 0, 0);

    return ret;
}

void assoc_procs(Process *p, uintptr_t i, Process *q, uintptr_t j) {
    RefCnt<Socket> server, client;
    Socket::pair(server, client);

    log(assoc_procs, "%p:%lu <-> %lu:%p\n", p, j, i, q);
    // file descriptors are 0-based, handles are 1-based (as null has special
    // meaning there).
    p->aspace->replace_file(j - 1, server);
    q->aspace->replace_file(i - 1, std::move(server));

    p->assoc_handles(j, q, i);
}

void init_modules(Cpu *cpu, const mboot::Info& info) {
    assert(info.has(mboot::Modules));
    auto mod = PhysAddr<mboot::Module>(info.mods_addr);
    const size_t count = info.mods_count;
    printf("%zu module(s)\n", count);
    Process **procs = new Process *[count];
    for (size_t n = 0; n < count; n++) {
        const char *name = PhysAddr<char>(mod->string);
        if (strchr(name, ' ')) name = strchr(name, ' ') + 1;
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
        cpu->queue(procs[i]);
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

NORETURN void page_fault(Process *p, u64 error) {
    log(page_fault, "page fault %lx cr2=%p rip=%p in %s\n",
        error, (void*)x86::cr2(), (void*)p->rip, p->name());

    assert(error & pf::User);
    i64 fault_addr = x86::cr2();
    assert(fault_addr >= 0);

    auto as = p->aspace.get();
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
    log(irq, "IRQ %d triggered, irq process is %s\n", vec, p->name());

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
        abort();
    }
    auto p = cpu->process;
    if (p) {
        if (log_int_entry_regs) {
            printf("Process registers (%s)\n", cpu->process->name());
            cpu->process->saved_regs.dump();
        }
        cpu->leave(p);
    } else {
        log(idle, "Got interrupt %u while idle\n", vec);
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
                cpu->queue(p);
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
