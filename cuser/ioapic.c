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

static const uintptr_t fresh = 0x100;
static const uintptr_t rawIRQ = 0x1;
#define MAX_GSI 64
// 48 is the 32 CPU exceptions plus 16 PIC interrupts still there to deal with
// spurious interrupts.
#define GSI_IRQ_BASE 49

typedef volatile u32 apic_page[4096 / 4];
static apic_page apic_pages[256] PLACEHOLDER_SECTION ALIGN(4096);

static void map_mmio(volatile void *p, uintptr_t physAddr, size_t size)
{
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
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
u8 apic_id_for_gsi[256];

// Handles for registered GSI clients. Set to 1 when registered.
u8 downstream_gsi[256];
// Handles for raw IRQs upstream
u8 upstream_irq[256] PLACEHOLDER_SECTION;

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

static void register_rawirq(ipc_arg_t irq, ipc_dest_t handle)
{
	hmod_copy(rawIRQ, handle);
	sendrcv1(MSG_REG_IRQ, handle, &irq);
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
		hmod_rename(h, (uintptr_t)apic);

		for (u64 i = 0; i <= max_redir; i++) {
			u8 gsi = gsibase + i;
			u8 irq = GSI_IRQ_BASE + gsi;
			//log("Registering IRQ %d for GSI %d\n", irq, gsi);
			register_rawirq(irq, (uintptr_t)&upstream_irq[irq]);
			apic_id_for_gsi[gsi] = id;
		}
	}
	else
	{
		send1(MSG_ACPI_ADD_IOAPIC, h, 0);
		hmod_delete(h);
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

static void reg_gsi(uintptr_t h, uintptr_t gsi, uintptr_t flags)
{
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

	send1(MSG_REG_IRQ, h, gsi);
	downstream_gsi[gsi] = 1;
	hmod_rename(h, (uintptr_t)&downstream_gsi[gsi]);
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
	if (downstream_gsi[gsi]) {
		pulse((uintptr_t)&downstream_gsi[gsi], 1);
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

void start() {
	__default_section_init();

	map(0, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
		lapic, apic_pbase, sizeof(lapic));

	for (;;) {
		ipc_arg_t arg1, arg2, arg3;
		ipc_dest_t rcpt = fresh;
		ipc_msg_t msg = recv3(&rcpt, &arg1, &arg2, &arg3);
		switch (msg & 0xff)
		{
		case MSG_PULSE:
			handle_irq(rcpt - (uintptr_t)upstream_irq);
			break;
		case MSG_IRQ_ACK:
			ack_irq(rcpt - (uintptr_t)downstream_gsi);
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
