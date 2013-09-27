#include "common.h"

/* Test some stuff - map some physical memory and inspect it. */

static const uintptr_t ALLOWED_FLAGS = PROT_READ | PROT_EXECUTE;
static const uintptr_t zeropage = 4;

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

	uintptr_t arg1 = 0x12340001;
	uintptr_t arg2 = 0x12340002;
	uintptr_t rcpt = zeropage;
	uintptr_t msg = ipc2(MSG_PFAULT, &rcpt, &arg1, &arg2);
	printf("test_maps got %x: %p %p\n", msg, arg1, arg2);

	/*while (addr < 32*1024*1024)*/ {
		map(zeropage, PROT_READ, (void*)pointer, addr, 4096);
		inspect(pointer, pointer + 4096);
	}
	for(;;);
}
