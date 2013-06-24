#include "common.h"

/* Test some stuff - map some physical memory and inspect it. */

static const uintptr_t ALLOWED_FLAGS = PROT_READ | PROT_EXECUTE;

extern void printf(const char* fmt, ...);

void inspect(const char* p, const char* end) {
	printf("Inspecting %x..%x\n", p, end);
	size_t n = 0;
	while (p < end) {
		uint64_t b = *(u64*)p;
		if (b) {
			printf("%x: %p\n", p, b);
			n++;
		}
		p += 8;
	}
	printf("%x non-zero bytes in %x..%x\n", n, p, end);
}

void start() {
	char* pointer = (char*)0x200000;
	uintptr_t addr = 0x1efc000;

	/*while (addr < 32*1024*1024)*/ {
		map(0, PROT_READ, (void*)pointer, addr, 4096);
		inspect(pointer, pointer + 4096);
	}
	for(;;);
}
