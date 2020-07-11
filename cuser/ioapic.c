#include <assert.h>

#include "common.h"
#include "msg_acpi.h"

#define LOG_ENABLED 0
#define log(fmt, ...) do { if (LOG_ENABLED) printf("ioapic: " fmt, ## __VA_ARGS__); } while (0)

static const uintptr_t apic_pbase = 0xfee00000;
static volatile u32 lapic[1024] PLACEHOLDER_SECTION ALIGN(4096);
#define REG(name, offset) \
	static const size_t name = (offset) / sizeof(u32)
REG(EOI, 0xb0);

static const uintptr_t rawIRQ = 0x1;
#define MAX_GSI 64
// 48 is the 32 CPU exceptions plus 16 PIC interrupts still there to deal with
// spurious interrupts.
#define GSI_IRQ_BASE 49

#define NUM_GSIS (MAX_GSI + 1)
#define NUM_IRQS 256

typedef volatile u32 apic_page[4096 / 4];
static apic_page apic_pages[256] PLACEHOLDER_SECTION ALIGN(4096);

static void map_mmio(volatile void *p, uintptr_t physAddr, size_t size)
{
	map(-1, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
		p, physAddr, size);
}

struct apic
{
	u8 id;
	u8 gsibase;
	volatile u32* mmio;
};
static struct apic apics[256];
enum {
	REGSEL = 0,
	REGWIN = 0x10 / sizeof(u32),
};
enum Reg {
	IOAPICID = 0,
	IOAPICVER = 1,
	IOAPICARB = 2,
	IOAPICRED = 0x10,
};

// Map each gsi to the apic id that handles it.
// 0 will be used for unknown GSIs, but this is also a valid APIC ID. Check the
// mmio pointer for an apic to see if it is initialized.
u8 apic_id_for_gsi[NUM_GSIS];

// Handles for registered GSI clients.
int downstream_gsi[NUM_GSIS];
// Handles for raw IRQs upstream
int upstream_irq[NUM_IRQS];

// General operation:
// * one or more MSG_ACPI_ADD_IOAPIC messages to register the I/O APIC's present
//   on the system, they are assigned ranges of interrupts starting from 48
//
//   An interrupt vector allocation API for those will help with e.g. the APIC
//   timer interrupt, currently hardcoded to interrupt 48.
// * users (ACPICA) register for interrupts with MSG_REG_IRQ
//   The argument is GSI, to be interpreted against the gsi base of each APIC.

static u32 read(struct apic* a, u32 reg)
{
	a->mmio[REGSEL] = reg;
	return a->mmio[REGWIN];
}

static void write(struct apic* a, u32 reg, u32 value)
{
	a->mmio[REGSEL] = reg;
	a->mmio[REGWIN] = value;
}

static void write_redirect(struct apic* a, u8 pin, u64 data)
{
	write(a, IOAPICRED + 2 * pin, data);
	write(a, IOAPICRED + 2 * pin + 1, data >> 32);
}

static u64 read_redirect(struct apic* a, u8 pin)
{
	u64 lo = read(a, IOAPICRED + 2 * pin);
	u64 hi = read(a, IOAPICRED + 2 * pin + 1);
	return (hi << 32) | lo;
}

static int register_rawirq(ipc_arg_t irq)
{
	sendrcv1(MSG_REG_IRQ, MSG_TX_ACCEPTFD | rawIRQ, &irq);
	return irq;
}

static void add_ioapic(uintptr_t h, u8 id, uintptr_t physAddr, u64 gsibase)
{
	log("Adding I/O APIC at %p (id %d, gsi base %d)\n",
			physAddr, id, gsibase);
	if (gsibase < MAX_GSI)
	{
		struct apic *apic = &apics[id];
		apic->id = id;
		apic->gsibase = gsibase;
		apic->mmio = apic_pages[id];
		map_mmio(apic->mmio, physAddr, 4096);

		u32 apicid = read(apic, IOAPICID);
		u32 ver = read(apic, IOAPICVER);
		log("IOAPICID=%#08x IOAPICVER=%#08x\n", apicid, ver);
		u8 max_redir = (ver >> 16) & 0xff;
		log("Found APIC version %#x with %d interrupts\n", ver & 0xff, max_redir + 1);

		send1(MSG_ACPI_ADD_IOAPIC, h, max_redir + 1);

		for (u64 i = 0; i <= max_redir; i++) {
			u8 gsi = gsibase + i;
			u8 irq = GSI_IRQ_BASE + gsi;
			upstream_irq[irq] = register_rawirq(irq);
			apic_id_for_gsi[gsi] = id;
		}
	}
	else
	{
		send1(MSG_ACPI_ADD_IOAPIC, h, 0);
	}
}

enum {
	RED_MASKED = 1 << 16,
	// Trigger mode: 1 = level, 0 = edge
	RED_TRIGGER_MODE = 1 << 15,
	RED_TRIGGER_MODE_LEVEL = RED_TRIGGER_MODE,
	RED_TRIGGER_MODE_EDGE = 0,
	// Read-only, for level-triggered interrupts.
	// Set to 1 when local APIC(s) accept the level-triggered interrupt.
	// Reset to 0 when an EOI with matching vector is received.
	RED_REMOTE_IRR = 1 << 14,
	RED_INTPOL_LOW = 1 << 13,
	RED_INTPOL_HIGH = 0 << 13,
	// Read-only. 0 = idle, 1 = send pending
	RED_DELIVERY_STATUS = 1 << 12,
	RED_DESTMOD = 1 << 11,
	RED_DESTMOD_LOGICAL = RED_DESTMOD,
	RED_DELMOD_MASK = 7 << 8,
	// 0 = Fixed, deliver to every destination CPU
	// 1 = Lowest Priority, use the TPR register to select CPU
	RED_DELMOD_LOWPRIO = 1 << 8,
	// 2 = SMI
	// 3 = Reserved
	// 4 = NMI
	// 5 = INIT
	// 6 = Reserved
	// 7 = ExtINT
};

static void reg_gsi(uintptr_t tx, uintptr_t gsi, uintptr_t flags)
{
	assert(tx & MSG_TX_ACCEPTFD);
	assert(gsi < MAX_GSI);
	struct apic* apic = &apics[apic_id_for_gsi[gsi]];
	assert(apic->mmio);

	u8 pin = gsi - apic->gsibase;
	u64 prev = read_redirect(apic, pin);
	// Fill out the redirection entry in the apic thing
	// Flags should specify some of the things we need to know here. Delivery
	// mode probably needs to be set up for some ACPI things (SMI, NMI).
	u64 x =
		// Destination: all CPUs
		((u64)0xff << 56)
		// Flags etc:
		| (flags & 1 ? RED_TRIGGER_MODE_EDGE : RED_TRIGGER_MODE_LEVEL)
		| (flags & 2 ? RED_INTPOL_LOW : RED_INTPOL_HIGH)
		| RED_DESTMOD_LOGICAL
		| RED_DELMOD_LOWPRIO
		// Vector:
		| (GSI_IRQ_BASE + gsi);
	write_redirect(apic, pin, x);
	log("Changed redirect from %#lx to %#lx\n", prev, read_redirect(apic, pin));

	int fds[2];
	socketpair(fds);

	downstream_gsi[gsi] = fds[0];
	send2(MSG_REG_IRQ, tx | MSG_TX_CLOSEFD, fds[1], gsi);
}

static void handle_irq(uintptr_t irq) {
	uintptr_t gsi = irq - GSI_IRQ_BASE;
	if (gsi >= MAX_GSI) return;

	struct apic* apic = &apics[apic_id_for_gsi[gsi]];
	log("Received IRQ %d (GSI %d)\n", (int)irq, gsi);
	if (apic->mmio) {
		u8 pin = gsi - apic->gsibase;
		// Mask the interrupt (until we get an IRQ_ACK back)
		u64 red_entry = read_redirect(apic, pin);
		write_redirect(apic, pin, red_entry | RED_MASKED);
		lapic[EOI] = 0;
	}
	if (downstream_gsi[gsi] >= 0) {
		pulse(downstream_gsi[gsi], 1);
	}
}

static void ack_irq(uintptr_t gsi) {
	if (gsi >= MAX_GSI) return;

	struct apic* apic = &apics[apic_id_for_gsi[gsi]];
	log("Ack'ed GSI %d\n", gsi);
	if (apic->mmio) {
		u8 pin = gsi - apic->gsibase;
		u64 red_entry = read_redirect(apic, pin);
		write_redirect(apic, pin, red_entry & ~RED_MASKED);
		log("Changed redirect from %#lx to %#lx\n", red_entry, read_redirect(apic, pin));
	}
}

static int irq_index_from_fd(int fd) {
	for (int i = 0; i < NUM_IRQS; i++) {
		if (upstream_irq[i] == fd) {
			log("matched fd %d to IRQ %d\n", fd, i);
			return i;
		}
	}
	log("no upstream IRQ for fd %d\n", fd);
	return -1;
}

static int gsi_index_from_fd(int fd) {
	for (int i = 0; i < NUM_GSIS; i++) {
		if (downstream_gsi[i] == fd) {
			log("matched fd %d to GSI %d\n", fd, i);
			return i;
		}
	}
	log("no downstream GSI for fd %d\n", fd);
	return -1;
}

void start() {
	__default_section_init();

	// Set to -1 since 0 is a valid file descriptor.
	memset(&upstream_irq, 0xff, sizeof(upstream_irq));
	memset(&downstream_gsi, 0xff, sizeof(downstream_gsi));

	map(-1, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
		lapic, apic_pbase, sizeof(lapic));

	for (;;) {
		ipc_arg_t arg1, arg2, arg3;
		ipc_dest_t rcpt = msg_set_fd(0, -1);
		ipc_msg_t msg = recv3(&rcpt, &arg1, &arg2, &arg3);
		log("Got %#lx from %#lx\n", msg & 0xff, rcpt);
		switch (msg & 0xff)
		{
		case SYS_PULSE:
			handle_irq(irq_index_from_fd(rcpt));
			break;
		case MSG_IRQ_ACK:
			ack_irq(gsi_index_from_fd(rcpt));
			break;
		case MSG_ACPI_ADD_IOAPIC:
			add_ioapic(rcpt, arg1, arg2, arg3);
			break;
		case MSG_REG_IRQ:
			reg_gsi(rcpt, arg1 & 0xff, arg1 >> 8);
			break;
		default:
			log("Unknown message %#lx from %#lx\n", msg & 0xff, rcpt);
		}
	}
}
