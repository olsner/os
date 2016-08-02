#include "common.h"

static const uintptr_t apic_handle = 4;
char trampoline_copy[4096] ALIGN(0x1000) PLACEHOLDER_SECTION;
typedef struct trampoline_data
{
	u64 semaphore;
} trampoline_data;
// 2 bytes should be enough for a jump past the trampoline data.
#define TRAMPOLINE_DATA_OFFSET 2

enum {
	IPI_INIT = 5,
	IPI_SIPI = 6,
};

static volatile struct trampoline_data *get_trampoline_data(void) {
	return (trampoline_data*)&trampoline_copy[TRAMPOLINE_DATA_OFFSET];
}

static void nanosleep(uintptr_t nsec) {
	static char dummy;
	static const uintptr_t handle = (uintptr_t)&dummy;
	hmod_copy(apic_handle, handle);
	sendrcv1(MSG_REG_TIMER, handle, &nsec);
	hmod_delete(handle);
}
static void usleep(uintptr_t usec) { nanosleep(usec * 1000); }

static void start_cpu(u8 apic_id) {
	uintptr_t trampoline_addr = syscall1(SYSCALL_ADDCPU, apic_id);
	uintptr_t vector_mask = 0xff000;
	// Must not be zero, must be page aligned, and the page number must fit
	// into one byte.
	assert(trampoline_addr);
	assert(!(trampoline_addr & ~vector_mask));
	// Will overwrite and replace the previous mapping, if any. The trampoline
	// address should be the same for all CPUs though.
	map(0, MAP_PHYS | PROT_READ,
		trampoline_copy, trampoline_addr, sizeof(trampoline_copy));

	printf("Trampoline code @%p:\n", trampoline_addr);
	hexdump(trampoline_copy, 0x80);

	const u64 init_ipi = apic_id | (IPI_INIT << 8) | 0;
	const u64 sipi = apic_id | (IPI_SIPI << 8) | (trampoline_addr << 4);
	printf("Sending INIT IPI to %u\n", apic_id);
	send1(MSG_APIC_SEND_IPI, apic_handle, init_ipi);
	usleep(10000);
	volatile struct trampoline_data *data = get_trampoline_data();
	for (unsigned n = 0; n < 2; n++) {
		printf("Sending STARTUP IPI to %u\n", apic_id);
		send1(MSG_APIC_SEND_IPI, apic_handle, sipi);
		usleep(2000000);
		printf("200usec have passed - response word is %#lx\n",
				data->semaphore);
		if (data->semaphore) {
			break;
		}
		printf("No response - retrying SIPI\n");
	}
}

void start() {
	__default_section_init();
	printf("start_cpu: ...\n");

	usleep(1000000);

	// Hardcode CPU 1 for now, should get info from ACPICA about the APIC IDs
	// to attempt to start (if any).
	start_cpu(1);

	printf("done.\n");
	abort();
}
