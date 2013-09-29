#include "common.h"

/* Test some stuff - map some physical memory and inspect it. */

static const uintptr_t ALLOWED_FLAGS = PROT_READ | PROT_EXECUTE;
static const uintptr_t zeropage = 4;

void inspect(const char* start, const char* end) {
	printf("Inspecting %x..%x\n", start, end);
	const u64* p = (u64*)start;
	size_t n = 0;
	while (p < (u64*)end) {
		uint64_t b = *p;
		if (b) {
			printf("%x: %p\n", p, b);
			n++;
		}
		p++;
	}
	printf("%x non-zero bytes in %x..%x\n", n, start, end);
}

void start() {
	char* pointer = (char*)0x200000;
	uintptr_t addr = 0x1234000;

	map(zeropage, PROT_READ, (void*)pointer, addr, 4096);

	uintptr_t arg1 = (uintptr_t)pointer;
	uintptr_t arg2 = PROT_READ;
	uintptr_t rcpt = 0;
	uintptr_t msg = ipc2(MSG_PFAULT, &rcpt, &arg1, &arg2);
	printf("test_maps got %x: %p %p\n", msg, arg1, arg2);

	/*while (addr < 32*1024*1024)*/ {
		inspect(pointer, pointer + 4096);
	}
	for(;;);
}
