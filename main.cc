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
typedef unsigned int uint;

extern "C" void start64() __attribute__((noreturn));
extern "C" void irq_entry(u8 vec, u64 err) __attribute__((noreturn));

#define DEBUGCON 1

#define STRING_INL_LINKAGE static
#include "string.c"

#define S_(X) #X
#define S(X) S_(X)
#define assert(X) \
	do { if (!(X)) { assert_failed(__FILE__ ":" S(__LINE__), #X "\n"); } } while (0)

namespace {

void assert_failed(const char* file, const char* line, const char* msg) __attribute__((noreturn));
void abort() __attribute__((noreturn));
void unimpl(const char* what) __attribute__((noreturn));

static const intptr_t kernel_base = -(1 << 30);

static constexpr void* PhysAddr(uintptr_t phys) {
	return (void*)(phys + kernel_base);
}
template <class T>
static constexpr T* HighAddr(T* lowptr) {
	return (T*)PhysAddr((uintptr_t)lowptr);
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
	static u16* const buffer = (u16*)PhysAddr(0xb80a0);
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
	}

	void write(const char *s) {
		while (char c = *s++) write(c);
	}
};

using Console::write;

void unimpl(const char *what) {
	write("UNIMPL: ");
	write(what);
	abort();
}

void abort() {
	for(;;);
}

void assert_failed(const char* fileline, const char* msg) {
	write(fileline); write(": ASSERT FAILED: "); write(msg);
	abort();
}

namespace x86 {
	enum class seg : u16 {
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
			u64 high = ((addr >> 16) & 0xffff) | (flags << 8);

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

// Symbols exported by start32.o
namespace start32 {
	extern "C" u32 memory_start;

	namespace low {
		extern "C" u32 mbi_pointer;
		extern "C" x86::gdtr gdtr;
		extern "C" u64 kernel_pdp[512];
	}

	static const x86::gdtr& gdtr = *HighAddr(&low::gdtr);

	// mboot::Info *mboot_info();
}

} // namespace

using Console::write;

void irq_entry(u8 vec, u64 err) {
	unimpl("irq_entry");
}

void start64() {
	write("Hello world\n");

	x86::lgdt(start32::gdtr);
	x86::ltr(x86::seg::tss64);
	idt::init();

	unimpl("rest of main");

	// memory init
	// per-cpu init, start
	//
	// init_modules using multiboot data
	// run the cpu

	abort();
}
