#include "common.h"
#include "msg_acpi.h"

#define log printf
#if 0
#define debug log
#else
#define debug(...) (void)0
#endif

static const uintptr_t acpi_handle = 6;
static const uintptr_t pic_handle = 6;
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

	// Missed Packets Count
	MPC = 0x4010 / 4,
	// Good Packets Received Count
	GPRC = 0x4074 / 4,
	// Good Packets Transmitted Count
	GPTC = 0x4080 / 4,

	RAL0 = 0x5400 / 4,
	RAH0 = 0x5404 / 4
};

enum interrupts
{
	// Transmit descriptor written
	IM_TXDW = 1 << 0,
	// Transmit queue empty
	IM_TXQE = 1 << 1,
	// Link status change
	IM_LSC = 1 << 2,
	// Receive descriptor minimum threshold
	IM_RXDMT0 = 1 << 4,
	// Receive overrun
	IM_RXO = 1 << 6,
	// Receiver timer
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
#define N_DESC (2048 / sizeof(union desc))

static struct descriptors {
	struct rdesc receive[N_DESC];
	struct tdesc transmit[N_DESC];
} descriptors PLACEHOLDER_SECTION ALIGN(0x1000);

#define receive_descriptors descriptors.receive
#define transmit_descriptors descriptors.transmit

// "rd" is not "ReaD", but "Receive Descriptor"
static uint rdhead, rdtail;
static uint tdhead, tdtail;
static uintptr_t gprc_total, mpc_total, gptc_total;
static u64 hwaddr0;

enum bufstate {
	// Buffer may not be used for receiving yet. The client may put data in the
	// buffer and want to send it any time.
	//
	// -> RECV_STARTED (async recv)
	// -> SEND_STARTED (async send)
	UNUSED = 0,
	RECV,
	SEND,
};

typedef struct buffer
{
	u8 state;
	u16 length;
	char* data;
} buffer;
static buffer* tdesc_owners[N_DESC];

#define BUFFER_SIZE 4096
// Temporary buffers for receiving packets from the NIC. These are always owned
// by the driver - since we don't know which client will receive a packet we
// have to receive, inspect and then forward it.
static char receive_buffers[N_DESC][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
#define NBUFS 16
// Buffers shared with clients. These get used both as receive and send buffers.
// Since we use them as send buffers directly, they have to be DMA allocated by
// us.
static char buffers[NBUFS][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
// Physical addresses of shared buffers.
static uintptr_t buffer_addr[NBUFS];
static buffer* buffer_owners[NBUFS];

#define PROTO_NBUFS 8
// protocol pointer doubles as handle for the protocol process
typedef struct protocol
{
	u16 ethertype;
	buffer buffers[PROTO_NBUFS];
} protocol;
#define MAX_PROTO 10
static protocol protocols[MAX_PROTO];
static size_t free_protocol;

u32 readpci32(u32 addr, u8 reg)
{
	assert(!(reg & 3));
	uintptr_t arg = addr << 8 | reg;
	sendrcv1(MSG_ACPI_READ_PCI, acpi_handle, &arg);
	return arg;
}

u16 readpci16(u32 addr, u8 reg)
{
	assert(!(reg & 1));
	u32 val = readpci32(addr, reg & 0xfc);
	if (reg & 2) {
		val >>= 16;
	}
	return val;
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

buffer* get_buffer_from_tdesc(int index)
{
	return tdesc_owners[index];
}

// Return the first still-not-finished descriptor in start..end
static int check_recv(int start, int end)
{
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		if (i == N_DESC) i = 0;
		u8 status = get_descriptor_status(i);
		if (!(status & RDESC_STAT_DD))
		{
			return i;
		}
		else if (status & RDESC_STAT_EOP)
		{
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
static buffer* buffer_for_recv(protocol* proto);
// Synchronously send the received status for buffer
// Must only be used if we know the client is already blocked for a reply.
static void send_recvd(protocol* proto, buffer* buf);

static bool incoming_packet(int start, int end) {
	size_t sum = 0;
	u16 ethtype = 0;
	// Are packets split *only* when they are too big or can they be split just
	// because?
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		size_t len = receive_descriptors[i].length;
		if (i == start)
		{
			u8* pkt = (u8*)receive_buffers[i];
			ethtype = read_u16be(pkt + 12);
		}
		sum += len;
	}
	debug("e1000: %ld bytes ethertype %04x\n", sum, ethtype);
	protocol* proto = find_proto_ethtype(ethtype);
	buffer* buf = proto ? buffer_for_recv(proto) : NULL;
	if (buf && sum <= 4096) {
		copy_packet(buf->data, start, end);
		*(u16*)(buf->data + 4094) = sum;
		size_t index = buf - proto->buffers;
		assert(buf->state == RECV);
		buf->state = UNUSED;
		pulse((uintptr_t)proto, 1 << index);
	} else {
		log("Delayed packet %d: %ld bytes ethertype %04x\n", start, sum, ethtype);
		return false;
	}
	for (int i = start; i != end; i = (i + 1) % N_DESC)
	{
		// We require that the status for descriptors in the queue is reset so
		// we can use the DD bit to check if it's done.
		receive_descriptors[i].status = 0;
	}
	return true;
}

void print_dev_state(void) {
	static const int link_speed[] = { 10, 100, 1000, 1000 };
	static u32 last_status;

	gprc_total += mmiospace[GPRC];
	mpc_total += mmiospace[MPC];
	gptc_total += mmiospace[GPTC];
	u32 status = mmiospace[STATUS];
	if (status != last_status) {
		last_status = status;
		log("STATUS: %#x link %s %dMb/s %s\n",
			status,
			status & STATUS_LU ? "up":"down",
			link_speed[(status & STATUS_SPEED_MASK) >> STATUS_SPEED_SHIFT],
			status & STATUS_FD ? "FD":"HD");
		log("GPRC: %lu MPC: %lu GPTC: %lu\n", gprc_total, mpc_total, gptc_total);
	}
}

static void recv_poll(void) {
	uint new_tail = rdtail;
	for (;;) {
		uint new_rdhead = check_recv(rdhead, rdtail);
		if (new_rdhead == rdhead)
			break;

		debug("e1000: done RX descriptors: %d..%d\n", rdhead, new_rdhead);
		if (!incoming_packet(rdhead, new_rdhead)) {
			debug("e1000: unable to receive\n");
			return;
		}

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
		debug("e1000: New RDTAIL = %d\n", new_tail);
		rdtail = new_tail;
		__barrier();
		mmiospace[RDTAIL] = rdtail;
	}
	debug("e1000: receive descriptor ring: %d..%d\n", rdhead, rdtail);
}

static protocol* get_proto_from_buffer(buffer* buf);
static void proto_ack_send(protocol* proto, buffer* buf);

// Some transmission finished, iterate from tdhead up looking for EOP+RS+DD
// packets.
static void tx_done(void) {
	uint new_head = tdhead;
	for (;;) {
		u8 status = get_tdesc_status(new_head);
		debug("e1000: TX descriptor %u status %02x\n", new_head, status);
		if (!(status & TDESC_STA_DD))
			break;

		transmit_descriptors[new_head].status = 0;
		transmit_descriptors[new_head].cmd = 0;
		buffer* buf = get_buffer_from_tdesc(new_head);
		if (buf) {
			protocol* proto = get_proto_from_buffer(buf);
			proto_ack_send(proto, buf);
		} else {
			debug("e1000: TX descriptor %u: no sender?\n", new_head);
		}
		debug("e1000: TX descriptor %u done\n", new_head);
		new_head++;
		new_head %= N_DESC;
	}
	if (new_head != tdhead)
	{
		debug("e1000: New TDHEAD = %u\n", new_head);
		tdhead = new_head;
		// We don't need to poke TDHEAD ourselves, the NIC does that.
	}
	debug("e1000: transmit descriptor ring %u..%u\n", tdhead, tdtail);
}

// Handle all interrupt causes set in ICR. Since reading ICR clears the
// register, we have to handle all of them.
void handle_icr(u32 icr) {
	debug("ICR: %#x\n", icr);
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
	if (icr & IM_LSC) {
		log("e1000: link status change.\n");
	}
	if (!(icr & (IM_TXDW | IM_RXT0 | IM_LSC))) {
		debug("Interrupt but I did nothing.\n");
	}
}

void handle_irq(void) {
	u32 icr;
	unsigned n = 0;
	if /*while*/ ((icr = mmiospace[ICR])) {
		handle_icr(icr);
		n++;
	}
	if (n > 1) {
		debug("e1000: needed %u loops of IRQ\n", n);
	}
}

static protocol* reg_proto(u16 ethertype) {
	if (free_protocol == MAX_PROTO) {
		return NULL;
	}
	log("e1000: protocol %x registered\n", ethertype);
	const size_t i = free_protocol++;
	protocol* proto = &protocols[i];
	memset(proto, 0, sizeof(protocol));
	proto->ethertype = ethertype;
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

static protocol* get_proto_from_buffer(buffer* buf) {
	size_t protoIx = ((char*)buf - (char*)protocols) / sizeof(protocol);
	protocol* proto = protocols + protoIx;
	assert(proto->buffers <= buf && buf < proto->buffers + NBUFS);
	return proto;
}

static char* allocate_buffer(buffer* buffer) {
	// Already allocated
	if (buffer->data) {
		return buffer->data;
	}
	for (size_t i = 0; i < NBUFS; i++) {
		if (!buffer_owners[i]) {
			buffer_owners[i] = buffer;
			buffer->data = buffers[i];
			return buffer->data;
		}
	}
	assert(!"ran out of buffers");
	return NULL;
}

static buffer* buffer_for_recv(protocol* proto) {
	for (size_t i = 0; i < NBUFS; i++) {
		buffer* buf = &proto->buffers[i];
		if (buf->state == RECV) {
			return buf;
		}
	}
	return NULL;
}

static void proto_recv(protocol* proto, u8 recv_buffer) {
	if (!proto) {
		return;
	}
	buffer* buf = proto->buffers + recv_buffer;
	debug("e1000: protocol %x receive on %d (state = %d)\n", proto->ethertype, recv_buffer, buf->state);
	assert(buf->state == UNUSED);
	assert(buf->data);
	buf->state = RECV;
	recv_poll();
}

static void proto_ack_send(protocol* proto, buffer* buf) {
	size_t index = buf - proto->buffers;
	debug("e1000: ack_send proto %d buffer %d (state = %d)\n", proto - protocols, index, buf->state);
	assert(buf->state == SEND);
	pulse((uintptr_t)proto, 1 << index);
	// We're done with the buffer so ownership goes back to the client.
	buf->state = UNUSED;
}

static bool queue_tx_desc(buffer* buf, u16 length) {
	// find a free transmit buffer and descriptor
	size_t i = tdtail, newtdtail = (tdtail + 1) % N_DESC;
	if (newtdtail == tdhead) {
		return false;
	}

	size_t bufferIndex = (char (*)[4096])buf->data - buffers;
	debug("e1000: queueing buffer %p (%d) for send\n", buf->data, bufferIndex);
	tdesc_owners[i] = buf;
	struct tdesc* desc = transmit_descriptors + i;
	desc->length = length;
	desc->status = 0;
	desc->buffer = buffer_addr[bufferIndex];
	// EOP: send this now, RS: report status and set DD when done.
	desc->cmd = TDESC_CMD_EOP | TDESC_CMD_RS | TDESC_CMD_IFCS;
	tdtail = newtdtail;
	debug("e1000: descriptor data %p %p\n", *(u64*)desc, ((u64*)desc)[1]);
	__barrier(); // FIXME Exactly which kind of barrier are we talking about?
	mmiospace[TDTAIL] = tdtail;
	debug("e1000: transmit ring now %u..%u\n", tdhead, tdtail);
	return true;
}

static void proto_send(protocol* proto, u8 send_buffer, uintptr_t length) {
	if (!proto) {
		return;
	}
	buffer* buf = proto->buffers + send_buffer;
	debug("e1000: protocol %x sends %d (= state %d)\n", proto->ethertype, send_buffer, buf->state);
	assert(buf->state == UNUSED && buf->data);
	buf->state = SEND;
	if (length > 4096) length = 4096;

	if (!queue_tx_desc(buf, length)) {
		log("e1000: out of transmit descriptors!\n");
		// Unfortunately, we don't say anything about failed sends :)
		proto_ack_send(proto, buf);
		return;
	}
}

static u64 read_eeprom(u8 word) {
	mmiospace[EERD] = EERD_START | (word << 8);
	while (!(mmiospace[EERD] & EERD_DONE));
	return (mmiospace[EERD] >> 16) & 0xffff;
}

// Supported PCI device ids (all with vendor 0x8086)
static const u16 pci_dev_ids[] = {
	// 82540EM PRO/1000 (emulated by bochs and qemu)
	0x100e,
	// 82579LM (Thinkpad x220)
	0x1502,
};
#define ARRAY_SIZE(xs) (sizeof(xs) / sizeof(*xs))

void start() {
	__default_section_init();

	uintptr_t arg;
	log("e1000: looking for PCI device...\n");
	for (size_t i = 0; i < ARRAY_SIZE(pci_dev_ids); i++) {
		arg = 0x80860000 | pci_dev_ids[i];
		sendrcv1(MSG_ACPI_FIND_PCI, acpi_handle, &arg);
		if (arg != ACPI_PCI_NOT_FOUND) {
			break;
		}
	}
	if (arg == ACPI_PCI_NOT_FOUND) {
		log("e1000: No devices found\n");
		goto fail;
	}
	log("e1000: found %x\n", arg);
	// bus << 8 | dev << 3 | func
	const uintptr_t pci_id = arg;
	uintptr_t arg2 = ACPI_PCI_CLAIM_MASTER | 1; // Just claim pin 0
	sendrcv2(MSG_ACPI_CLAIM_PCI, acpi_handle, &arg, &arg2);
	if (!arg) {
		log("e1000: failed :(\n");
		goto fail;
	}
	arg2 &= 0xffff;
	const u8 irq = arg2 & 0xff;
	const u8 triggering = !!(arg2 & 0x100);
	const u8 polarity = !!(arg2 & 0x200);
	log("e1000: claimed! irq %x triggering %d polarity %d\n", irq, triggering, polarity);
	hmod_copy(pic_handle, pin0_irq_handle);
	sendrcv1(MSG_REG_IRQ, pin0_irq_handle, &arg2);

	u32 cmd = readpci16(pci_id, PCI_COMMAND);
	assert(cmd & PCI_COMMAND_MASTER);
	debug("PCI CMD: %04x master=%d\n", cmd, !!(cmd & PCI_COMMAND_MASTER));

	u32 bar0 = readpci32(pci_id, PCI_BAR_0);
	debug("BAR 0: %s %s %s: %x\n",
		bar0 & 1 ? "io" : "mem",
		(bar0 >> 1) & 3 ? "64-bit" : "32-bit",
		(bar0 >> 3) & 1 ? "prefetchable" : "non-prefetchable",
		bar0 & ~0xf);
	uintptr_t mmiobase = bar0 & ~0xf;
	if (((bar0 >> 1) & 3) == 2) // 64-bit
	{
		uintptr_t bar1 = readpci32(pci_id, PCI_BAR_1);
		debug("BAR0 was 64-bit, adding %x:%x.\n", bar1, mmiobase);
		mmiobase |= bar1 << 32;
	}
	debug("Mapping mmiospace %p to BAR %p\n", (void*)mmiospace, mmiobase);
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
		(void*)mmiospace, mmiobase, sizeof(mmiospace));

	debug("Status: %x\n", mmiospace[STATUS]);

	log("e1000: mmiospace hardware address %04x%08x, valid=%d\n", mmiospace[RAH0] & 0xffff, mmiospace[RAL0], !!(mmiospace[RAH0] & 0x80000000));
	if (!(mmiospace[RAH0] & 0x80000000)) {
		log("e1000: No valid hwaddr (yet), trying to read from EEPROM.\n");
		hwaddr0 = read_eeprom(0) | (read_eeprom(1) << 16) | (read_eeprom(2) << (u64)32);
		log("e1000: EEPROM hardware address %012lx\n", hwaddr0 & 0xffffffffffff);
		mmiospace[RAL0] = hwaddr0;
		mmiospace[RAH0] = (hwaddr0 >> 32) | 0x80000000;
	}

	hwaddr0 = mmiospace[RAL0] | ((u64)mmiospace[RAH0] << 32);
	log("e1000: hardware address %012lx\n", hwaddr0 & 0xffffffffffff);
	debug("e1000: address valid = %d\n", (hwaddr0 >> 63) & 1);
	debug("e1000: address select = %d\n", (hwaddr0 >> 48) & 3);
	hwaddr0 &= 0xffffffffffff;

	// Allocate receive descriptors and buffers
	uintptr_t descriptorPhysAddr =
		(uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE, (void*)&descriptors,
			0, sizeof(descriptors));

	for (size_t i = 0; i < N_DESC; i++) {
		// Note: DMA memory must still be allocated page by page
		uintptr_t physAddr = (uintptr_t)map(0,
				MAP_DMA | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
				(void*)receive_buffers[i], 0, sizeof(receive_buffers[i]));
		init_descriptor((union desc*)receive_descriptors + i, physAddr);
		// Associated to buffers on-demand.
		init_descriptor((union desc*)transmit_descriptors + i, 0);
	}

	for (size_t i = 0; i < NBUFS; i++) {
		// Note: DMA memory must still be allocated page by page
		// TODO Should the buffers be uncachable too?
		uintptr_t physAddr = (uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE,
				(void*)buffers[i], 0, sizeof(buffers[i]));
		// Just to fault the pages in so we can forward them later.
		memset(buffers[i], 0, sizeof(buffers[i]));
		buffer_addr[i] = physAddr;
		debug("transmit_buffer %u: %p -> phys %p\n", i, buffers[i], physAddr);
	}

	uintptr_t physAddr = descriptorPhysAddr + offsetof(struct descriptors, receive);
	debug("RX descriptor ring space at %p phys\n", physAddr);
	mmiospace[RDBAL] = physAddr;
	mmiospace[RDBAH] = physAddr >> 32;

	mmiospace[RDLEN] = sizeof(receive_descriptors);
	rdhead = 0; rdtail = N_DESC - 1;
	mmiospace[RDHEAD] = rdhead;
	mmiospace[RDTAIL] = rdtail;

	physAddr = descriptorPhysAddr + offsetof(struct descriptors, transmit);
	debug("TX descriptor ring space at %p phys\n", physAddr);
	mmiospace[TDBAL] = physAddr;
	mmiospace[TDBAH] = physAddr >> 32;

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
	// Note: disabled because we can only send <4kB packets to the recipients.
	// BAM, bit 15 = 1 - receive broadcast messages without filtering
	// BSIZE, bit 17:16 = 11b (256/4096 bytes)
	// DPF, bit 22 = 1 (discard pause frames)
	// BSEX, bit 25 = 1b
	mmiospace[RCTL] = RCTL_EN | /*RCTL_LPE |*/ RCTL_BAM | RCTL_DPF | RCTL_BSIZE_4096;
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
	// * RXT0. With the receive timer set to 0 (disabled), this trigger for
	//   every received package.
	// * TXDW. Fires when transmit descriptors have been written back.
	mmiospace[IMS] = IM_RXT0 | IM_TXDW | IM_LSC;

	for(;;) {
		print_dev_state();
		uintptr_t rcpt = fresh;
		arg = 0;
		arg2 = 0;
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		debug("e1000: received %x from %x: %x %x\n", msg, rcpt, arg, arg2);
		if (rcpt == pin0_irq_handle && msg == MSG_PULSE) {
			// Disable all interrupts, then ACK receipt to PIC
			send1(MSG_IRQ_ACK, rcpt, arg);
			handle_irq();
			continue;
		}

		if (rcpt == fresh) {
			if ((msg & 0xff) == MSG_ETHERNET_REG_PROTO) {
				protocol* proto = reg_proto(arg & 0xffff);
				log("e1000: registered ethertype %04x => %p\n", arg & 0xffff, proto);
				hmod(rcpt, (uintptr_t)proto, 0);
				send1(MSG_ETHERNET_REG_PROTO, (uintptr_t)proto, hwaddr0);
			} else {
				hmod_delete(rcpt);
			}
			continue;
		}

		switch (msg & 0xff)
		{
		case MSG_ETHERNET_RECV:
			proto_recv(find_proto(rcpt), arg);
			break;
		case MSG_ETHERNET_SEND: {
			u8 buffer = arg;
			u16 length = arg2;
			proto_send(find_proto(rcpt), buffer, length);
			break;
		}
		case MSG_PFAULT: {
			protocol* proto = find_proto(rcpt);
			if (proto) {
				arg >>= 12;
				debug("e1000: fault for protocol %x buffer %d\n", proto->ethertype, arg);
				if (arg < NBUFS) {
					arg = (uintptr_t)allocate_buffer(proto->buffers + arg);
					arg2 &= PROT_READ | PROT_WRITE;
				} else {
					arg = 0;
					arg2 = 0;
				}
				debug("e1000: granting %p (%x) to protocol %x\n", arg, arg2, proto->ethertype);
				ipc2(MSG_GRANT, &rcpt, &arg, &arg2);
			} else {
				debug("e1000: fault for unknown protocol (rcpt %#lx)\n", rcpt);
				assert(proto);
			}
			break;
		}
		}
	}
fail:
	abort();
}
