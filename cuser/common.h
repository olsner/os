#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

__BEGIN_DECLS

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef unsigned int uint;

// FIXME This causes 'start' to follow various silly calling conventions - such
// as saving callee-save registers. Find some way to get rid of that...
// Or wait for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=38534 to be fixed.
void start(void) __attribute__((noreturn,section(".start")));

// Symbols exposed by the linker script
extern char __bss_start[1];
extern char __bss_end[1];
extern char __data_lma[1];
extern char __data_lma_end[1];
extern char __data_size[1];
extern char __data_vma[1];

// Attribute for putting variable in a special placeholder section. Useful for
// reserving virtual memory space or dummy memory space for handles.
#define S__(x) #x
#define S_(x) S__(x)
#define PLACEHOLDER_SECTION __attribute__((section(".placeholder." __FILE__ "." S_(__LINE__))))
#define ALIGN(n) __attribute__((aligned(n)))

static void prefault_range(void* start, size_t size, int prot) {
	uint8_t* p = (uint8_t*)start;
	uint8_t* end = p + size;
	while (p < end) {
		prefault(p, prot);
		p += 4096;
	}
}

static void __default_section_init(void) {
	map_anon(PROT_READ | PROT_WRITE, __bss_start, __bss_end - __bss_start);
	memcpy(__data_vma, __data_lma, (uintptr_t)&__data_size);
}

static void hexdump(char* data, size_t length) {
	size_t pos = 0;
	while (pos < length) {
		printf("\n%04x: ", pos);
		for (int i = 0; i < 16 && pos < length; i++) {
			printf("%02x ", (u8)data[pos++]);
		}
	}
	printf("\n");
}

// For header type 0 (!)
// Is this part of the ACPI message API? Or maybe independent PCI utils.
enum pci_regs
{
	PCI_VENDOR_ID = 0x00,
	PCI_DEVICE_ID = 0x02,
	PCI_COMMAND   = 0x04,
	PCI_STATUS    = 0x06,
	PCI_REV_ID    = 0x08,
	PCI_PROG_IF   = 0x09,
	PCI_SUBCLASS  = 0x0a,
	PCI_CLASS     = 0x0b,
	PCI_CL_SIZE   = 0x0c,
	PCI_LATENCY_TIMER = 0x0d,
	PCI_HEADER_TYPE = 0x0e,
	PCI_BIST      = 0x0f,
	PCI_BAR_0     = 0x10,
	PCI_BAR_1     = 0x14,
	// .. BAR5
	// Cardbus CIS Pointer
	PCI_SS_VENDOR_ID = 0x2c,
	PCI_SUBSYSTEM_ID = 0x2e,
};
enum pci_command_bits
{
	PCI_COMMAND_IOSPACE = 1,
	PCI_COMMAND_MEMSPACE = 2,
	PCI_COMMAND_MASTER = 4,
};

/**
 * A simple (compiler) barrier. Memory writes to volatile variables before the
 * barrier must not be moved to after the barrier, regardless of optimizations.
 */
static void __barrier(void) {
	__asm__ __volatile__ ("":::"memory");
}

__END_DECLS

#endif // __COMMON_H
