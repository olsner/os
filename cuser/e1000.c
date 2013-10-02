#include "common.h"

//#define printf(...) (void)0

static const uintptr_t acpi_handle = 4;
static const uintptr_t pic_handle = 2;
static const uintptr_t pin0_irq_handle = 0x100;
static const uintptr_t fresh = 0x101;

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
struct tdesc
{
	u64 buffer;
	u16 length;
	// "Should be written with 0b for future compatibility"
	u8 checksumOffset;
	u8 cmd;
	// upper nibble reserved
	u8 status;
	u8 checksumStart;
	u16 special;
};
_Static_assert(sizeof(struct tdesc) == 16, "transmit decriptor size doesn't match spec");
enum
{
	TDESC_CMD_EOP = 1 << 0,
	TDESC_CMD_IFCS = 1 << 1,
	TDESC_CMD_RS = 1 << 3,
};
enum
{
	TDESC_STA_DD = 1
};

union desc
{
	struct rdesc rdesc;
	struct tdesc tdesc;
	u64 buffer;
};
_Static_assert(sizeof(union desc) == 16, "decriptor sizes don't match spec");

// Ugh. The specification gives byte offsets for these, and most are 8-byte
// aligned but with 4-byte registers. For use as indexes in mmiospace, the byte
// offset has to be divided by 4.
enum regs
{
	CTRL = 0,
	STATUS = 2,
	EERD = 0x14 / 4,
	ICR = 0xc0 / 4,
	// Interrupt Throttle Register. Bochs seems to ignore this.
	ITR = 0xc4 / 4,
	IMS = 0xd0 / 4,
	IMC = 0xd8 / 4,
	RCTL = 0x100 / 4,
	TCTL = 0x400 / 4,
	RDBA = 0x2800 / 4,
	RDBAL = RDBA,
	RDBAH,
	RDLEN,
	RDHEAD = RDLEN + 2, // (RDH in intel docs)
	RDTAIL = RDHEAD + 2, // (RDT in intel docs)

	TDBA = 0x3800 / 4,
	TDBAL = TDBA,
	TDBAH,
	TDLEN,
	TDHEAD = TDLEN + 2, // TDH/TDT in intel docs
	TDTAIL = TDHEAD + 2,

	// Good Packets Received Count
	GPRC = 0x4074 / 4,

	RAL0 = 0x5400 / 4,
	RAH0 = 0x5404 / 4
};

enum interrupts
{
	IM_TXDW = 1 << 0,
	IM_RXDMT0 = 1 << 4,
	IM_RXT0 = 1 << 7
};
enum
{
	CTRL_ASDE = 1 << 5,
	CTRL_SLU = 1 << 6,
};
enum
{
	STATUS_FD = 1 << 0,
	STATUS_LU = 1 << 1,
	// 3..2: Function ID (00 or 01)
	// 4: TXOFF
	// 5: TBIMODE
	STATUS_SPEED_SHIFT = 6,
	STATUS_SPEED_MASK = 3 << STATUS_SPEED_SHIFT,
	// more stuff...
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
enum
{
	// EN, bit 1 = 1 - enable transmission
	TCTL_EN = 1 << 1,
	// PSP, bit 3 = 1 - pad short packets
	TCTL_PSP = 1 << 3,
	// CT, bit 11:4 = collision threshold (Recommended value - 0Fh)
	TCTL_CT_SHIFT = 4,
	// COLD, bit 21:12 = COLlision Distance, in byte times (Recommended value,
	// half duplex - 512 byte times, full duplex 64 byte times)
	TCTL_COLD_SHIFT = 12,
};
enum
{
	EERD_START = 1,
	EERD_DONE = 1 << 4,
};

#define ALIGN(n) __attribute__((aligned(n)))

static volatile u32 mmiospace[128 * 1024 / 4] PLACEHOLDER_SECTION ALIGN(128*1024);

// See RDLEN in e1000 manual, must be a multiple of 8 to make the total (byte)
// size of the descriptor ring a multiple of 128.
// Maximum is 65536 (a descriptor ring size of 1MB)
#define N_DESC (128 / sizeof(union desc))

static struct descriptors {
	struct rdesc receive[N_DESC];
	struct tdesc transmit[N_DESC];
} descriptors PLACEHOLDER_SECTION ALIGN(0x1000);

#define receive_descriptors descriptors.receive
#define transmit_descriptors descriptors.transmit

#define BUFFER_SIZE 4096
static char receive_buffers[N_DESC][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
static char transmit_buffers[N_DESC][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);

// "rd" is not "ReaD", but "Receive Descriptor"
static uint rdhead, rdtail;
static uint tdhead, tdtail;
static uintptr_t gprc_total;
static u64 hwaddr0;

// protocol pointer doubles as handle for the protocol process
typedef struct protocol
{
	u16 ethertype;
	char *receive_buffer;
	char *send_buffer;
	bool unacked_recv;
	bool sending;
} protocol;
#define MAX_PROTO 10
static protocol protocols[MAX_PROTO];
static size_t free_protocol;
static char proto_buffers[2 * MAX_PROTO][BUFFER_SIZE] ALIGN(BUFFER_SIZE);

u32 readpci32(u32 addr, u8 reg)
{
	uintptr_t arg = addr << 8 | (reg & 0xfc);
	sendrcv1(MSG_ACPI_READ_PCI, acpi_handle, &arg);
	return arg;
}

void init_descriptor(union desc* desc, uintptr_t physAddr)
{
	memset(desc, 0, sizeof(union desc));
	desc->buffer = physAddr;
}

int get_descriptor_status(int index)
{
	return ((volatile struct rdesc*)receive_descriptors + index)->status;
}

int get_tdesc_status(int index)
{
	return ((volatile struct tdesc*)transmit_descriptors + index)->status;
}

#define FOR_DESCS(i, start, end) \
	for (int i = start; i != end; i = (i + 1) % N_DESC)

// Return the first still-not-finished descriptor in start..end
static int check_recv(int start, int end)
{
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		if (i == N_DESC) i = 0;
		u8 status = get_descriptor_status(i);
		printf("e1000: RX descriptor %d: status %#x\n", i, status);
		if (!(status & RDESC_STAT_DD))
		{
			return i;
		}
		else if (status & RDESC_STAT_EOP)
		{
			printf("e1000: RX packet at %d..%d\n", start, i);
			return (i + 1) % N_DESC;
		}
	}
	return start;
}

static u16 read_u16be(const void* p_) {
	const u8* p = p_;
	return ((u16)p[0] << 8) | p[1];
}

static void copy_packet(void* dest, int start, int end) {
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		size_t len = receive_descriptors[i].length;
		memcpy(dest, receive_buffers[i], len);
		dest = (char*)dest + len;
	}
}

static protocol* find_proto_ethtype(u16 ethertype);

static void incoming_packet(int start, int end) {
	size_t sum = 0;
	u16 ethtype = 0;
	// Are packets split *only* when they are too big or can they be split just
	// because?
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		size_t len = receive_descriptors[i].length;
		if (i == start) {
			u8* pkt = (u8*)receive_buffers[i];
			ethtype = read_u16be(pkt + 12);
		}
		sum += len;
	}
	printf("e1000: %ld bytes ethertype %04x\n", sum, ethtype);
	protocol* proto = find_proto_ethtype(ethtype);
	if (proto && !proto->unacked_recv && sum <= 4096) {
		copy_packet(proto->receive_buffer, start, end);
		printf("e1000: after copy packet, ethtype=%04x\n", read_u16be(proto->receive_buffer + 12));
		hexdump(proto->receive_buffer, sum);
		proto->unacked_recv = true;
		send2(MSG_ETHERNET_RCVD, (uintptr_t)proto, 0, sum);
	} else {
		printf("Unhandled packet: %ld bytes ethertype %04x\n", sum, ethtype);
	}
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		// We require that the status for descriptors in the queue is reset so
		// we can use the DD bit to check if it's done.
		receive_descriptors[i].status = 0;
	}
}

void print_dev_state(void) {
	static const int link_speed[] = { 10, 100, 1000, 1000 };
	u32 status = mmiospace[STATUS];
	printf("STATUS: %#x link %s %dMb/s %s\n",
		status,
		status & STATUS_LU ? "up":"down",
		link_speed[(status & STATUS_SPEED_MASK) >> STATUS_SPEED_SHIFT],
		status & STATUS_FD ? "FD":"HD");
	gprc_total += mmiospace[GPRC];
	printf("GPRC: %lu\n", gprc_total);
}

static void recv_poll(void) {
	uint new_tail = rdtail;
	for (;;) {
		uint new_rdhead = check_recv(rdhead, rdtail);
		if (new_rdhead == rdhead)
			break;

		printf("e1000: done RX descriptors: %d..%d\n", rdhead, new_rdhead);
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
		printf("e1000: New RDTAIL = %d\n", new_tail);
		rdtail = new_tail;
		// TODO Write barrier or something may be required before writing to
		// RDTAIL to make sure our changes to the descriptors are visible.
		mmiospace[RDTAIL] = rdtail;
	}
	printf("e1000: receive descriptor ring: %d..%d\n", rdhead, rdtail);
}

// Some transmission finished, iterate from tdhead up looking for EOP+RS+DD
// packets.
static void tx_done() {
	uint new_head = tdhead;
	for (;;) {
		u8 status = get_tdesc_status(new_head);
		printf("e1000: TX descriptor %u status %02x\n", new_head, status);
		if (!(status & TDESC_STA_DD))
			break;

		transmit_descriptors[new_head].status = 0;
		transmit_descriptors[new_head].cmd = 0;
		printf("e1000: TX descriptor %u done\n", new_head);
		new_head++;
		new_head %= N_DESC;
	}
	if (new_head != tdhead)
	{
		printf("e1000: New TDHEAD = %u\n", new_head);
		tdhead = new_head;
		// We don't need to poke TDHEAD ourselves, the NIC does that.
	}
	printf("e1000: transmit descriptor ring %u..%u\n", tdhead, tdtail);
}

void handle_irq(void) {
	// NB: This clears all pending interrupts. We must handle all causes set to
	// 1.
	u32 icr = mmiospace[ICR];
	printf("ICR: %#x (%#x)\n", icr, mmiospace[ICR]);
	printf("IMS: %#x\n", mmiospace[IMS]);
	if (icr & IM_RXT0) {
		// Receive timer timeout. Receive some messages.
		recv_poll();
	}
	if (icr & IM_RXDMT0) {
		// RXDMT0, Receive Descriptor Minimum Threshold Reached
	}
	if (icr & IM_TXDW) {
		tx_done();
	}
}

static protocol* reg_proto(u16 ethertype, u8 recv_buffers) {
	if (free_protocol == MAX_PROTO) {
		return NULL;
	}
	printf("e1000: protocol %x registered\n", ethertype);
	const size_t i = free_protocol++;
	protocol* proto = &protocols[i];
	proto->ethertype = ethertype;
	proto->receive_buffer = proto_buffers[2 * i + 0];
	proto->send_buffer = proto_buffers[2 * i + 1];
	/* These are really just to fault in the pages so we can later grant them
	 * without having support for recursive faults. */
	memset(proto->receive_buffer, 0, BUFFER_SIZE);
	memset(proto->send_buffer, 0, BUFFER_SIZE);
	proto->unacked_recv = false;
	proto->sending = false;
	return proto;
}

static protocol* find_proto(uintptr_t rcpt) {
	uintptr_t p = (uintptr_t)protocols;
	uintptr_t i = (rcpt - p) / sizeof(protocol);
	if (i < free_protocol && rcpt == (p + i * sizeof(protocol))) {
		return (protocol*)rcpt;
	}
	return NULL;
}

static protocol* find_proto_ethtype(u16 ethertype) {
	for (unsigned i = 0; i < free_protocol; i++) {
		protocol* proto = &protocols[i];
		if (proto->ethertype == ethertype || proto->ethertype == ETHERTYPE_ANY) {
			return proto;
		}
	}
	return NULL;
}

static void proto_ack_recv(protocol* proto, u8 recv_buffer) {
	if (!proto) {
		return;
	}
	printf("e1000: protocol %x acks recv %d\n", proto->ethertype, recv_buffer);
	proto->unacked_recv = false;
	assert(recv_buffer == 0);
}

static void proto_send(protocol* proto, u8 send_buffer, uintptr_t length) {
	if (!proto) {
		return;
	}
	assert(send_buffer == 1);
	printf("e1000: protocol %x sends %d\n", proto->ethertype, send_buffer);
	proto->sending = true;
	if (length > 4096) length = 4096;
	// find a free transmit buffer and descriptor
	size_t i = tdtail, newtdtail = (tdtail + 1) % N_DESC;
	if (newtdtail == tdhead) {
		printf("e1000: out of transmit descriptors!\n");
		// ran out of descriptors. (We keep tdhead up to date with descriptors
		// marked as finished in the TXDW interrupt.)
		send1(MSG_ETHERNET_SEND, (uintptr_t)proto, send_buffer);
		return;
	}
	// copy some data, set up length, reset status, set command.
	memcpy(transmit_buffers[i], proto->send_buffer, length);
	struct tdesc* desc = transmit_descriptors + i;
	desc->length = length;
	desc->status = 0;
	// EOP: send this now, RS: report status and set DD when done.
	desc->cmd = TDESC_CMD_EOP | TDESC_CMD_RS | TDESC_CMD_IFCS;
	tdtail = newtdtail;
	printf("e1000: descriptor data %p %p\n", *(u64*)desc, ((u64*)desc)[1]);
	mmiospace[TDTAIL] = tdtail;
	printf("e1000: transmit ring now %u..%u\n", tdhead, tdtail);
	// since we always copy the data (or drop the packet entirely) we can
	// actually send the reply immediately. Unfortunately it deadlocks?
	send1(MSG_ETHERNET_SEND, (uintptr_t)proto, send_buffer);
}

static u64 read_eeprom(u8 word) {
	mmiospace[EERD] = EERD_START | (word << 8);
	while (!(mmiospace[EERD] & EERD_DONE));
	return (mmiospace[EERD] >> 16) & 0xffff;
}

void start() {
	__default_section_init();

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

	hwaddr0 = read_eeprom(0) | (read_eeprom(1) << 16) | (read_eeprom(2) << (u64)32);
	printf("e1000: EEPROM hardware address %012lx\n", hwaddr0 & 0xffffffffffff);

	mmiospace[RAL0] = hwaddr0;
	mmiospace[RAH0] = (hwaddr0 >> 32) | 0x80000000;

	hwaddr0 = mmiospace[RAL0] | ((u64)mmiospace[RAH0] << 32);
	printf("e1000: hardware address %012lx\n", hwaddr0 & 0xffffffffffff);
	printf("e1000: address valid = %d\n", (hwaddr0 >> 63) & 1);
	printf("e1000: address select = %d\n", (hwaddr0 >> 48) & 3);
	hwaddr0 &= 0xffffffffffff;

	// Allocate receive descriptors and buffers
	uintptr_t descriptorPhysAddr =
		(uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE, (void*)&descriptors,
			0, sizeof(descriptors));
	uintptr_t physAddr = descriptorPhysAddr + offsetof(struct descriptors, receive);
	printf("RX descriptor ring space at %p phys\n", physAddr);
	mmiospace[RDBAL] = physAddr;
	mmiospace[RDBAH] = physAddr >> 32;

	for (size_t i = 0; i < N_DESC; i++) {
		// Note: DMA memory must still be allocated page by page
		uintptr_t physAddr = (uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE,
				(void*)receive_buffers[i], 0, sizeof(receive_buffers[i]));
		init_descriptor((union desc*)receive_descriptors + i, physAddr);
	}

	mmiospace[RDLEN] = sizeof(receive_descriptors);
	rdhead = 0; rdtail = N_DESC - 1;
	mmiospace[RDHEAD] = rdhead;
	mmiospace[RDTAIL] = rdtail;

	physAddr = descriptorPhysAddr + offsetof(struct descriptors, transmit);
	printf("TX descriptor ring space at %p phys\n", physAddr);
	mmiospace[TDBAL] = physAddr;
	mmiospace[TDBAH] = physAddr >> 32;

	for (size_t i = 0; i < N_DESC; i++) {
		// Note: DMA memory must still be allocated page by page
		uintptr_t physAddr = (uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE,
				(void*)transmit_buffers[i], 0, sizeof(transmit_buffers[i]));
		printf("transmit_buffer %u: %p -> phys %p\n", i, transmit_buffers[i], physAddr);
		init_descriptor((union desc*)transmit_descriptors + i, physAddr);
	}
	mmiospace[TDLEN] = sizeof(transmit_descriptors);
	tdhead = 0; tdtail = 0;
	mmiospace[TDHEAD] = 0;
	mmiospace[TDTAIL] = 0;

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
	// Enable transmit:
	// EN, bit 1 = 1 - enable transmission
	// PSP, bit 3 = 1 - pad short packets
	// CT, bit 11:4 = collision threshold (Recommended value - 0Fh)
	// COLD, bit 21:12 = COLlision Distance, in byte times (Recommended value,
	// half duplex - 512 byte times, full duplex 64 byte times)
	mmiospace[TCTL] = TCTL_EN | TCTL_PSP | (0xf << TCTL_CT_SHIFT) | (0x40 << TCTL_COLD_SHIFT);

	// Set some stuff: Set Link Up, Auto Speed Detect Enable
	// Clear: PHY Reset, VME (VLAN Enable)
	mmiospace[CTRL] = CTRL_ASDE | CTRL_SLU;
	// TODO Enable some subset of interrupts
	// * RXT0. With the receive timer set to 0 (disabled), this trigger for
	//   every received package.
	mmiospace[IMS] = IM_RXT0 | IM_TXDW;

	for(;;) {
		print_dev_state();
		uintptr_t rcpt = fresh;
		arg = 0;
		arg2 = 0;
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		printf("e1000: received %x from %x: %x %x\n", msg, rcpt, arg, arg2);
		if (rcpt == pin0_irq_handle && msg == MSG_IRQ_T) {
			handle_irq();
			send1(MSG_IRQ_ACK, rcpt, arg);
			continue;
		}
		if (rcpt == fresh) {
			if ((msg & 0xff) == MSG_ETHERNET_REG_PROTO) {
				protocol* proto = reg_proto(arg & 0xffff, arg2 & 0xff);
				printf("e1000: registered ethertype %04x => %p\n", arg & 0xffff, proto);
				hmod(rcpt, (uintptr_t)proto, 0);
				send1(MSG_ETHERNET_REG_PROTO, (uintptr_t)proto, hwaddr0);
			} else {
				hmod_delete(rcpt);
			}
			continue;
		}

		switch (msg & 0xff)
		{
		case MSG_ETHERNET_RCVD:
			proto_ack_recv(find_proto(rcpt), arg);
			break;
		case MSG_ETHERNET_SEND:
			assert(arg == 1); // only one send/recv buffer supported so far
			proto_send(find_proto(rcpt), arg, arg2);
			break;
		case MSG_PFAULT: {
			protocol* proto = find_proto(rcpt);
			if (proto) {
				arg >>= 12;
				printf("e1000: fault for protocol %x buffer %d\n", proto->ethertype, arg);
				if (arg < 1 /*proto->recv_buffers*/) {
					arg = (uintptr_t)proto->receive_buffer;
					arg2 = PROT_READ;
				} else {
					arg = (uintptr_t)proto->send_buffer;
					arg2 = PROT_READ | PROT_WRITE;
				}
				printf("e1000: granting %p to protocol %x\n", arg, proto->ethertype);
				ipc2(MSG_GRANT, &rcpt, &arg, &arg2);
			}
			break;
		}
		}
	}
fail:
	abort();
}
