#pragma once

namespace {
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
}
