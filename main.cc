#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <new>

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

extern "C" void start64() NORETURN;

#define DEBUGCON 1

#define STRING_INL_LINKAGE static UNUSED
#include "string.c"

namespace {

void assert_failed(const char* fileline, const char* msg) NORETURN;
void abort() NORETURN;
void unimpl(const char* what) NORETURN;

}

#define S_(X) #X
#define S(X) S_(X)
#define assert(X) \
    do { if (!(X)) { assert_failed(__FILE__ ":" S(__LINE__), #X "\n"); } } while (0)

#define log_idle 1
#define log_switch 1
#define log_runqueue 1
#define log(scope, ...) do { \
    if (log_ ## scope) { printf(__VA_ARGS__); } \
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
void *operator new[](size_t sz) {
    return malloc(sz);
}
void operator delete[](void *p) {
    free(p);
}

namespace {

namespace Console {
    void write(char c);
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
    while (c--) Console::write(*str++);
    return n;
}
void fputc_unlocked(char c, FILE *) {
    Console::write(c);
}
long int strtol(const char *, char **, int) {
    unimpl("strtol");
}
bool isdigit(char c) {
    return c >= '0' && c <= '9';
}

#define XPRINTF_NOSTDLIB
#define XPRINTF_NOERRNO
#define XPRINTF_LINKAGE static UNUSED
#include "xprintf.cpp"

#define printf xprintf

static const intptr_t kernel_base = -(1 << 30);

template <class T>
static constexpr T* PhysAddr(uintptr_t phys) {
    return (T*)(phys + kernel_base);
}
template <class T>
static constexpr T* HighAddr(T* lowptr) {
    return PhysAddr<T>((uintptr_t)lowptr);
}

static void memset16(u16* dest, u16 value, size_t n) {
    if (/* constant(value) && */ (value >> 8) == (value & 0xff)) {
        memset(dest, value, n * 2);
    } else {
        // rep movsw
        while (n--) *dest++ = value;
    }
}

static void debugcon_putc(char c) {
#if DEBUGCON
    asm("outb %0,%1"::"a"(c),"d"((u16)0xe9));
#endif
}

namespace Console {
    static u16* const buffer = PhysAddr<u16>(0xb80a0);
    static u16 pos;
    static const u8 width = 80, height = 24;

    void clear() {
        pos = 0;
        memset16(buffer, 0, width * height);
    }

    void write(char c) {
        if (c == '\n') {
            u8 fill = width - (pos % width);
            while(fill--) buffer[pos++] = 0;
        } else {
            buffer[pos++] = 0x0700 | c;
        }
        debugcon_putc(c);
        if (pos == width * height) {
            memmove(buffer, buffer + width, sizeof(*buffer) * width * (height - 1));
            pos -= width;
            memset16(buffer + pos, 0, width);
        }
    }

    void write(const char *s) {
        while (char c = *s++) write(c);
    }
};

using Console::write;

void unimpl(const char *what) {
    printf("UNIMPL: %s\n", what);
    abort();
}

void abort() {
    asm("cli;hlt");
    __builtin_unreachable();
}

void assert_failed(const char* fileline, const char* msg) {
    write(fileline); write(": ASSERT FAILED: "); write(msg);
    abort();
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
    u64 get_cr3() {
        u64 cr3;
        asm volatile("movq %%cr3, %0" : "=r"(cr3));
        return cr3;
    }
    void set_cr3(u64 cr3) {
        if (cr3 != get_cr3()) {
            asm volatile("movq %0, %%cr3" :: "r"(cr3));
        }
    }
}

namespace idt {
    using x86::seg;

    const u64 GatePresent = 0x80;
    const u64 GateTypeInterrupt = 0x0e;

    struct Entry {
        u64 low, high;

        Entry() {}
        Entry(void (*fn)(void)) {
            u64 addr = (u64)fn;
            u64 low = (addr & 0xffff) | ((u64)seg::code << 16);
            u64 flags = GatePresent | GateTypeInterrupt;
            u64 high = (addr & 0xffff0000) | (flags << 8);

            this->low = low | (high << 32);
            this->high = addr >> 32;
        }
    };
    const size_t N_ENTRIES = 49;
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
        extern "C" u32 irq_handlers[17];
    }

    void init() {
        static Table idt_table;
        idt_table[7] = Entry(handler_NM_stub);
        idt_table[14] = Entry(handler_PF_stub);
        for (int i = 32; i < 49; i++) {
            idt_table[i] = Entry((void(*)())&irq_handlers[i - 32]);
        }
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

#include "dlist.h"
#include "mem.h"
#include "aspace.h"
using aspace::AddressSpace;
#include "proc.h"
using proc::Process;
#include "cpu.h"
using cpu::Cpu;
#include "syscall.h"

Process *new_proc_simple(u32 start, u32 end_unaligned) {
    u32 end = (end_unaligned + 0xfff) & ~0xfff;
    u32 start_page = start & ~0xfff;
    auto aspace = new AddressSpace();
    auto ret = new Process(aspace);
    ret->regs.rsp = 0x100000;
    ret->rip = 0x100000 + (start & 0xfff);

    using namespace aspace;
    aspace->mapcard_set(0x0ff000, 0, 0, MAP_ANON | MAP_RW);
    aspace->mapcard_set(0x100000, 0, start_page - 0x100000, MAP_PHYS | MAP_RX);
    aspace->mapcard_set(0x100000 + (end - start_page), 0, 0, 0);

    return ret;
}

void assoc_procs(Process *p, uintptr_t i, Process *q, uintptr_t j) {
    /*if (log_assoc_procs)*/ printf("%p:%lu <-> %lu:%p\n", p, i, j, q);
    p->assoc_handles(j, q, i);
}

void init_modules(Cpu *cpu, const mboot::Info& info) {
    assert(info.has(mboot::Modules));
    auto mod = PhysAddr<mboot::Module>(info.mods_addr);
    const size_t count = info.mods_count;
    printf("%zu modules\n", count);
    Process **procs = new Process *[count];
    for (size_t n = 0; n < count; n++) {
        printf("Module %#x..%#x: %s\n",
            mod->start, mod->end, PhysAddr<char>(mod->string));
        procs[n] = new_proc_simple(mod->start, mod->end);
        printf("Module %#x..%#x: %p\n",
            mod->start, mod->end, procs[n]);
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

} // namespace

extern "C" void irq_entry(u8 vec, u64 err, Cpu *cpu) NORETURN;

void irq_entry(u8 vec, u64 err, Cpu *cpu) {
    printf("irq_entry(%u, %#lx, cpu=%p)\n", vec, (long)err, cpu);
    auto p = cpu->process;
    p->unset(proc::Running);
    switch (vec) {
    case 7:
        unimpl("handler_NM");
        break;
    case 14:
        unimpl("PF");
        // assert(p);
        // page_fault(p, err);
        break;
    default:
        if (vec >= 32) {
            if (p) {
                cpu->queue(p);
            }
            unimpl("generic_irq_handler(vec);");
        }
    }
    cpu->run();
}

void start64() {
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
