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

#define DEBUGCON 1

#define STRING_INL_LINKAGE static
#include "string.c"

#define S_(X) #X
#define S(X) S_(X)
#define assert(X) \
	do { if (!(X)) { assert_failed(__FILE__ ":" S(__LINE__), #X "\n"); } } while (0)

namespace {

void assert_failed(const char* file, const char* line, const char* msg) __attribute__((noreturn));

static const intptr_t kernel_base = -(1 << 30);

static constexpr void* PhysAddr(uintptr_t phys) {
	return (void*)(phys + kernel_base);
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

void assert_failed(const char* fileline, const char* msg) {
	using Console::write;
	write(fileline); write(": ASSERT FAILED: "); write(msg);
	for(;;);
}

} // namespace

using Console::write;

void start64() {
	write("Hello world\n");
	assert(!"Testing the assert");
	for(;;);
}
