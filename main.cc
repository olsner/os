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

namespace {

static const intptr_t kernel_base = -(1 << 30);

static void* PhysAddr(uintptr_t phys) {
	return (void*)(phys + kernel_base);
}

class Console {
	u16* buffer;
	u16 pos;
	u8 width, height;

public:
	Console(void* buffer, u8 width = 80, u8 height = 24):
		buffer((u16*)buffer), width(width), height(height)
	{}
	Console();

	void clear(u16 c = 0) {
		pos = 0;
		size_t n = width * height;
		while (n--) {
			buffer[n] = c;
		}
	}

	void write(char c);
	void write(const char *s) {
		while (char c = *s++) write(c);
	}
};

void Console::write(char c) {
	if (c == '\n') {
		u8 fill = width - (pos % width);
		while (fill--) {
			write(' ');
		}
	} else {
		buffer[pos++] = 0x0700 | c;
	}
}

static Console gCon;

static void write(const char *s) {
	gCon.write(s);
}

} // namespace

void start64() {
	new (&gCon) Console(PhysAddr(0xb80a0));
	gCon.clear();
	write("Hello world\n");
	for(;;);
}
