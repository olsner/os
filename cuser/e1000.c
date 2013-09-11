#include "common.h"

static const uintptr_t acpi_handle = 4;
static const uintptr_t pic_handle = 2;
static const uintptr_t pin0_irq_handle = 0x100;

struct rdesc
{
	u64 buffer;
	u16 length;
	u16 checksum;
	u8 status;
	u8 errors;
	u16 special;
};
_Static_assert(sizeof(struct rdesc) == 16, "receive decriptor size doesn't match spec");
enum
{
	// Descriptor Done
	RDESC_STAT_DD = 1 << 0,
	// End Of Packet
	RDESC_STAT_EOP = 1 << 1,

};

// Ugh. The specification gives byte offsets for these, and most are 8-byte
// aligned but with 4-byte registers. For use as indexes in mmiospace, the byte
// offset has to be divided by 4.
enum regs
{
	CTRL = 0,
	STATUS = 2,
	ICR = 0xc0 / 4,
	ITR = 0xc4 / 4,
	IMS = 0xd0 / 4,
	IMC = 0xd8 / 4,
	RCTL = 0x100 / 4,
	RDBA = 0x2800 / 4,
	RDBAL = RDBA,
	RDBAH,
	RDLEN,
	RDHEAD = RDLEN + 2, // (RDH in intel docs)
	RDTAIL = RDHEAD + 2, // (RDT in intel docs)

	// Good Packets Received Count
	GPRC = 0x4074 / 4
};

enum interrupts
{
	IM_RXT0 = 1 << 7
};
enum
{
	CTRL_ASDE = 1 << 5,
	CTRL_SLU = 1 << 6,
};
enum
{
	// enable
	RCTL_EN = 1 << 1,
	// long packet enable
	RCTL_LPE = 1 << 5,
	// Broadcast Accept Mode
	RCTL_BAM = 1 << 15,
	// Buffer Size Extension
	RCTL_BSEX = 1 << 25,
	// BSIZE: 0..3 << 16
	// Combine with BSEX to multiply by 16.
	// 0,1,2 = 2048,1024,512
	RCTL_BSIZE_256 = 3 << 16,
	RCTL_BSIZE_4096 = RCTL_BSEX | RCTL_BSIZE_256,
	// Discard Pause Frames
	RCTL_DPF = 1 << 22,
};

#define ALIGN(n) __attribute__((aligned(n)))

static volatile u32 mmiospace[128 * 1024 / 4] PLACEHOLDER_SECTION ALIGN(128*1024);

// See RDLEN in e1000 manual, must be a multiple of 8 to make the total (byte)
// size of the descriptor ring a multiple of 128.
// Maximum is 65536 (a descriptor ring size of 1MB)
#define N_DESC (128 / sizeof(struct rdesc))

static struct rdesc receive_descriptors[N_DESC] PLACEHOLDER_SECTION ALIGN(0x1000);

#define BUFFER_SIZE 4096
static char receive_buffers[N_DESC][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);

static int rdhead, rdtail;
static uintptr_t gprc_total;

u32 readpci32(u32 addr, u8 reg)
{
	uintptr_t arg = addr << 8 | (reg & 0xfc);
	sendrcv1(MSG_ACPI_READ_PCI, acpi_handle, &arg);
	return arg;
}

void init_descriptor(struct rdesc* desc, uintptr_t physAddr)
{
	memset(desc, 0, sizeof(struct rdesc));
	desc->buffer = physAddr;
}

int get_descriptor_status(int index)
{
	return ((volatile struct rdesc*)receive_descriptors + index)->status;
}

#define FOR_DESCS(i, start, end) \
	for (int i = start; i != end; i = (i + 1) % N_DESC)

// Return the first still-not-finished descriptor in start..end
int check_recv(int start, int end)
{
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		if (i == N_DESC) i = 0;
		u8 status = get_descriptor_status(i);
		printf("Descriptor %d: status %#x\n", i, status);
		if (!(status & RDESC_STAT_DD))
		{
			return i;
		}
		else if (status & RDESC_STAT_EOP)
		{
			printf("Packet at %d..%d\n", start, i);
			return (i + 1) % N_DESC;
		}
	}
	return start;
}

void incoming_packet(int start, int end) {
	size_t sum = 0;
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		size_t len = receive_descriptors[i].length;
		printf("Desc %d: %ld bytes\n", i, len);
		// TODO Print a nice hex dump of the contents
		sum += len;
	}
	printf("Packet received: %ld bytes\n", sum);
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		// We require that the status for descriptors in the queue is reset so
		// we can use the DD bit to check if it's done.
		receive_descriptors[i].status = 0;
	}
}

void print_dev_state(void) {
	printf("STATUS: %#x\n", mmiospace[STATUS]);
	gprc_total += mmiospace[GPRC];
	printf("GPRC: %lu\n", gprc_total);
}

static void recv_poll(void) {
	int new_tail = rdtail;
	for (;;) {
		int new_rdhead = check_recv(rdhead, rdtail);
		if (new_rdhead == rdhead)
			break;

		printf("Finished descriptors: %d..%d\n", rdhead, new_rdhead);
		incoming_packet(rdhead, new_rdhead);

		// Device's real RDHEAD is somewhere "between" new_rdhead and rdtail
		// new_rdhead <= rdh < rdtail (if new_rdhead < rdtail)
		// new_rdhead <= rdh || rdh < rdtail (otherwise)
		// We want to advance rdtail as far as possible while making it
		// guaranteed not equal to the device's real rdhead.

		new_tail = new_rdhead == N_DESC ? 0 : (new_rdhead - 1) % N_DESC;
		rdhead = new_rdhead;
	}
	if (new_tail != rdtail)
	{
		printf("New tail = %d\n", new_tail);
		rdtail = new_tail;
		// TODO Write barrier or something may be required before writing to
		// RDTAIL to make sure our changes to the descriptors are visible.
		mmiospace[RDTAIL] = rdtail;
	}
	printf("New descriptor ring: %d..%d\n", rdhead, rdtail);
}

void handle_irq(void) {
	// NB: This clears all pending interrupts. We must handle all causes set to
	// 1.
	u32 icr = mmiospace[ICR];
	printf("ICR: %#x (%#x)\n", icr, mmiospace[ICR]);
	printf("IMS: %#x\n", mmiospace[IMS]);
	if (icr & 0x80) {
		// Receive timer timeout. Receive some messages.
		recv_poll();
	}
	if (icr & 0x10) {
		// RXDMT0, Receive Descriptor Minimum Threshold Reached
	}
}

void start() {
	__default_section_init();

	uintptr_t dummy = 0;
	uintptr_t arg = 0x8086100e; // 8086:100e PCI ID for 82540EM PRO/1000
	printf("e1000: looking for PCI device...\n");
	sendrcv1(MSG_ACPI_FIND_PCI, acpi_handle, &arg);
	if (!arg)
	{
		printf("e1000: No devices found\n");
		goto fail;
	}
	printf("e1000: found %x\n", arg);
	// bus << 8 | dev << 3 | func
	const uintptr_t pci_id = arg;
	uintptr_t arg2 = 1; // Just claim pin 0
	sendrcv2(MSG_ACPI_CLAIM_PCI, acpi_handle, &arg, &arg2);
	if (!arg)
	{
		printf("e1000: failed :(\n");
		goto fail;
	}
	const u8 irq = arg2 &= 0xff;
	printf("e1000: claimed! irq %x\n", irq);
	hmod(pic_handle, pic_handle, pin0_irq_handle);
	sendrcv1(MSG_REG_IRQ, pin0_irq_handle, &arg2);

	u32 bar0 = readpci32(pci_id, PCI_BAR_0);
	printf("BAR 0: %s %s %s: %x\n",
		bar0 & 1 ? "io" : "mem",
		(bar0 >> 1) & 3 ? "64-bit" : "32-bit",
		(bar0 >> 3) & 1 ? "prefetchable" : "non-prefetchable",
		bar0 & ~0xf);
	uintptr_t mmiobase = bar0 & ~0xf;
	if (((bar0 >> 1) & 3) == 2) // 64-bit
	{
		uintptr_t bar1 = readpci32(pci_id, PCI_BAR_1);
		printf("BAR0 was 64-bit, adding %x:%x.\n", bar1, mmiobase);
		mmiobase |= bar1 << 32;
	}
	printf("Mapping mmiospace %p to BAR %p\n", (void*)mmiospace, mmiobase);
	// TODO Map unprefetchable!
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE,
		(void*)mmiospace, mmiobase, sizeof(mmiospace));

	u32 status = mmiospace[STATUS];
	printf("Status: %x\n", status);

	// Allocate receive descriptors and buffers
	uintptr_t physAddr = (uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE,
			(void*)receive_descriptors, 0, sizeof(receive_descriptors));
	printf("Allocated descriptor ring space at %p phys\n", physAddr);
	mmiospace[RDBAL] = physAddr;
	mmiospace[RDBAH] = physAddr >> 32;

	for (int i = 0; i < N_DESC; i++) {
		uintptr_t physAddr = (uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE,
				(void*)receive_buffers[i], 0, sizeof(receive_buffers[i]));
		init_descriptor(receive_descriptors + i, physAddr);
	}

	mmiospace[RDLEN] = sizeof(receive_descriptors);
	rdhead = 0; rdtail = N_DESC - 1;
	mmiospace[RDHEAD] = rdhead;
	mmiospace[RDTAIL] = rdtail;

	// ITR (0xc4), lower 16 bits = number of 256ns intervals between interrupts
	// "A initial suggested range is 651-5580 (28Bh - 15CCh)."
	// Now set to 0 to get as many interrupts as possible.
	mmiospace[ITR] = 0;
	mmiospace[IMC] = -1; // Disable all interrupts

	// Enable receive:
	// EN, bit 1 = 1 - enable reception
	// LPE, bit 5 = 1 - enable long packet reception
	// BAM, bit 15 = 1 - receive broadcast messages without filtering
	// BSIZE, bit 17:16 = 11b (256/4096 bytes)
	// DPF, bit 22 = 1 (discard pause frames)
	// BSEX, bit 25 = 1b
	mmiospace[RCTL] = RCTL_EN | RCTL_LPE | RCTL_BAM | RCTL_DPF | RCTL_BSIZE_4096;

	// Set some stuff: Set Link Up, Auto Speed Detect Enable
	// Clear: PHY Reset, VME (VLAN Enable)
	mmiospace[CTRL] = CTRL_ASDE | CTRL_SLU;
	// TODO Enable some subset of interrupts
	// * RXT0. With the receive timer set to 0 (disabled), this trigger for
	//   every received package.
	mmiospace[IMS] = IM_RXT0;

	for(;;) {
		print_dev_state();
		//recv_poll();
		uintptr_t rcpt = 0;
		arg = 0;
		arg2 = 0;
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		printf("e1000: received %x from %x: %x %x\n", msg, rcpt, arg, arg2);
		if (rcpt == pin0_irq_handle && msg == MSG_IRQ_T) {
			handle_irq();
			send1(MSG_IRQ_ACK, rcpt, arg);
			continue;
		}
		switch (msg)
		{
		}
	}
fail:
	abort();
}
