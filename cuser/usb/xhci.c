#include "common.h"

#define log(fmt, ...) printf("xhci: " fmt, ## __VA_ARGS__)
#if 1
#define debug log
#else
#define debug(...) (void)0
#endif

static const uintptr_t acpi_handle = 4;
static const uintptr_t pic_handle = 2;
static const uintptr_t pin0_irq_handle = 0x100;
static const uintptr_t fresh = 0x101;

static volatile u8 mmiospace[128 * 1024] PLACEHOLDER_SECTION ALIGN(128*1024);
// NB: Byte offsets
enum HostCntrlCapRegs
{
	// u8
	CAPLENGTH = 0,
	// u8 rsvd
	// u16
	HCIVERSION = 2,
	// u32
	HCSPARAMS1 = 4,
	HCSPARAMS2 = 8,
	HCSPARAMS3 = 0x0c,
	HCCPARAMS1 = 0x10,
	// Doorbell Offset. Bits 1:0 are reserved, so with those masked out you
	// have a byte offset, alternatively bits 31:2 are a dword offset.
	DBOFF = 0x14,
	// Runtime Register Space Offset, bits 4:0 are reserved, bits 31:5 are
	// a 32-byte-aligned offset from mmiobase.
	RTSOFF = 0x18,
	HCCPARAMS2 = 0x1c,
};
enum HCCP1Bits
{
	HCCP1_AC64 = 1,
	// Plus others... I'm not sure those are interesting.
};
// Offset into mmiospace based on values in capability registers
static volatile u32* opregs;
// Note: dword offsets
enum OperationalRegisters
{
	USBCMD = 0,
	USBSTS,
	PAGESIZE,
	// 3,4: RsvdZ
	DNCTRL = 5,
	// Command Ring Control Register
	CRCR = 6, // u64
	CRCRH = 7,
	// 8..11, RsvdZ
	DCBAAP = 12,
	DCBAAPH,
	CONFIG,
	// RsvdZ up to 0x400
	PORTREGSET = 256,
};
enum USBCMDBits
{
	USBCMD_Run = 1,
	USBCMD_Reset = 2,
	USBCMD_IntEnable = 4,
};
enum USBSTSBits
{
	USBSTS_Halted = 1,
	USBSTS_NotReady = 1 << 11,
};
enum CRCRBits
{
	CRCR_RingCycleState = 1,
	CRCR_CommandStop = 2,
	CRCR_CommandAbort = 4,
	CRCR_CommandRingRunning = 8
};
enum PortStatusRegisters
{
	PSCR = 0,
	PORTPMSC = 1,
	PORTLI = 2,
	PORTHLPMC = 3,
	NUMREGS = 4, // Number of dwords per port - 0x10 bytes => 4 dwords.
};
static volatile u32* doorbells;
static volatile u32* runtimeregs;
enum RuntimeRegs
{
	MFINDEX = 0,
};
enum InterruptRegs
{
	IMAN = 0,
	IMOD = 1,
	ERSTSZ = 2,
	// reserved
	ERSTBA = 4,
	ERSTBAH = 5,
	ERDP = 6,
	ERDPH = 7,
};
enum IMANBits
{
	IMAN_IntPending = 1,
	IMAN_IntEnabled = 2,
};
static volatile u32 *interruptregs;

static u32 read_mmio32(unsigned byte_offset)
{
	return *(volatile u32*)(mmiospace + byte_offset);
}
static u16 read_mmio16(unsigned byte_offset)
{
	u32 res = read_mmio32(byte_offset & -4);
	if (byte_offset & 2) {
		res >>= 16;
	}
	return res;
}

typedef struct command_trb
{
	u64 address;
	u32 status;
	u32 command;
} command_trb;
_Static_assert(sizeof(struct command_trb) == 16, "command TRB size doesn't match spec");

typedef struct erse
{
	u64 base;
	// number of TRBs in this Event Ring Segment, >= 16, < 4096, rounded up to
	// nearest multiple of 4 to make the Event Ring size 64 byte aligned.
	u64 size;
} erse;
static erse create_erse(uintptr_t physAddr, u16 ringSize)
{
	erse res;
	res.base = physAddr;
	res.size = ringSize;
	return res;
};

struct normal_trb
{
	u64 address;
	u16 length;
	u8 td_size;
	u8 interrupter_target;
	u16 flags;
	u16 reserved;
};
_Static_assert(sizeof(struct normal_trb) == 16, "normal TRB size doesn't match spec");

enum TRBTypes
{
	// 0 = reserved
	// 1..8: generally transfer ring only
	TRB_Normal = 1,
	TRB_SetupStage = 2,
	TRB_DataStage = 3,
	TRB_StatusStage = 4,
	TRB_Isoch = 5,
	// Link: Command and Transfer but not Event Ring
	TRB_Link = 6,
	TRB_EventData = 7,
	TRB_NoOp = 8,
	// 9..23: commmands
	TRB_CMD_EnableSlot = 9,
	TRB_CMD_DisableSlot = 10,
	TRB_CMD_AddressDevice = 11,
	TRB_CMD_ConfigureEndpoint = 12,
	TRB_CMD_NoOp = 23,

	TRB_PortStatusChange = 34,
};
enum TRBFlags
{
	TRB_Cycle = 1,
	TRB_ToggleC = 2,
	TRB_Chain = 1 << 4,
	TRB_IOC = 1 << 5,
};
typedef union trb
{
	struct command_trb cmd;
	struct normal_trb normal;
	struct {
		u64 parameter;
		u32 status;
		u32 control;
	};
} trb;

static union trb create_link_trb(u64 address, u8 flags)
{
	union trb res;
	res.parameter = address;
	res.status = 0;
	res.control = flags | (TRB_Link << 10);
	return res;
}

#define COMMAND_RING_SIZE 16
#define EVENT_RING_SIZE 16
// The Event Ring Segment Table also has 16-byte entries
#define ERST_SIZE 1
static struct page1 {
	command_trb command_ring[COMMAND_RING_SIZE];
	trb event_ring[EVENT_RING_SIZE];
	erse erst[ERST_SIZE];
} page1 PLACEHOLDER_SECTION ALIGN(4096);
static volatile struct command_trb *command_enqueue = page1.command_ring;
// The PCS bit to put into created TRBs
static bool command_pcs;
static volatile u64 device_context_addrs[256] PLACEHOLDER_SECTION ALIGN(4096);

enum EcpCapIds
{
	CAPID_SUPPORTED_PROTOCOL = 2,
};
static void iterate_ecps(volatile const u32* ecp)
{
	while (ecp)
	{
		u32 ecpr = ecp[0];
		u8 capid = ecpr & 0xff;
		u8 next = (ecpr >> 8) & 0xff;
		switch (capid)
		{
		case CAPID_SUPPORTED_PROTOCOL:
		{
			u64 nm = ecp[1];
			u32 ports = ecp[2];
			debug("%p: Supported protocol %s %x.%x, %u ports at %u\n", ecp,
				&nm, ecpr >> 24, (ecpr >> 16) & 0xff,
				(ports >> 8) & 0xff, ports & 0xff);
			break;
		}
		default:
			debug("Found cap %u at %p\n", capid, ecp);
		}
		ecp = next ? ecp + next : NULL;
	}
}

// xHCI host controller driver. Talks to "the USB system" and provides
// communication between USB class drivers and the devices.

/* Random Notes...
 *
 * - device connected:
 *   1. Enable Slot to get a Slot ID
 *   2. Put a Device Context at the provided index
 *   3. Address Device to signal finished Device Context and to give the USB
 *      device its address.
 *      * Address Device TRB points to an "Input Context"
 *
 * - device configuration:
 *    SET_CONFIG/SET_INTERFACE must be preceded by a Configure Endpoint command,
 *    that tells the xhci which endpoints will be active, and to confirm that
 *    bandwidth and resources are available.
 *
 * - what are scratchpad buffers?
 */

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

u8 readpci8(u32 addr, u8 reg)
{
	u16 val = readpci16(addr, reg & 0xfe);
	if (reg & 1) {
		val >>= 8;
	}
	return val;
}

static void handle_irq()
{
	log("IRQ");
}

void start()
{
	// FIXME So much copy-pasta from xhci.c here - extract more PCI utility
	// functions somewhere.
	__default_section_init();

	uintptr_t arg;
	log("looking for PCI device...\n");
	// Serial bus controller (0x0c), USB controller (0x03),
	// USB 3.0 controller (0x30) => xHCI controller.
	arg = (u64)0x0c0330 << 32;
	sendrcv1(MSG_ACPI_FIND_PCI, acpi_handle, &arg);
	if (arg == ACPI_PCI_NOT_FOUND) {
		log("No devices found\n");
		goto fail;
	}
	log("found %x\n", arg);
	// bus << 8 | dev << 3 | func
	const uintptr_t pci_id = arg;
	uintptr_t arg2 = ACPI_PCI_CLAIM_MASTER | 1; // Just claim pin 0
	sendrcv2(MSG_ACPI_CLAIM_PCI, acpi_handle, &arg, &arg2);
	if (!arg) {
		log("failed :(\n");
		goto fail;
	}
	const u8 irq = arg2 &= 0xff;
	log("claimed! irq %x\n", irq);
	hmod(pic_handle, pic_handle, pin0_irq_handle);
	sendrcv1(MSG_REG_IRQ, pin0_irq_handle, &arg2);

	{
		u32 cmd = readpci16(pci_id, PCI_COMMAND);
		debug("PCI CMD: %04x master=%d\n", cmd, !!(cmd & PCI_COMMAND_MASTER));
		//writepci16(pci_id, PCI_COMMAND, cmd | PCI_COMMAND_MASTER);
		u16 status = readpci16(pci_id, PCI_STATUS);
		debug("PCI STATUS: %04x int=%d\n", status, !!(status & PCI_STATUS_INTERRUPT));
	}

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

	u8 sbrn = readpci8(pci_id, 0x60);
	debug("SBRN is %#x\n", sbrn);

	u8 caplength = mmiospace[CAPLENGTH];
	debug("Capabilities length %u\n", caplength);
	opregs = (u32*)(mmiospace + caplength);
	doorbells = (u32*)(mmiospace + (read_mmio32(DBOFF) & -4));
	runtimeregs = (u32*)(mmiospace + (read_mmio32(RTSOFF) & -32));
	interruptregs = runtimeregs + (0x20 / 4);
	debug("Operational regs at %p, doorbells at %p, runtimeregs at %p\n",
			opregs, doorbells, runtimeregs);
	debug("HCI version %#x\n", read_mmio16(HCIVERSION));

	u32 hcsparams1 = read_mmio32(HCSPARAMS1);
	u8 device_maxports = hcsparams1 & 0xff;
	debug("Max ports/slots/intrs: %u/%u/%u\n", device_maxports,
			(hcsparams1 >> 24) & 0xff, (hcsparams1 >> 8) & 0x7ff);
	u32 hcsparams2 = read_mmio32(HCSPARAMS2);
	u32 max_scratchpads =
		((hcsparams2 >> (21 - 5)) & (0x1f << 5)) | ((hcsparams2 >> 27) & 0x1f);
	debug("Max Scratchpad Buffers = %u\n", max_scratchpads);
	// Scratchpads not supported - need to allocate the Scratchpad Buffer Array
	assert(max_scratchpads == 0);
	debug("IST: %u %s\n", hcsparams2 & 7, hcsparams2 & 8 ? "Frames" : "Microframes");
	u32 hccparams1 = read_mmio32(HCCPARAMS1);
	debug("hccparams1: %#x\n", hccparams1 & 0xffff);
	assert(hccparams1 & HCCP1_AC64);
	u16 xECP = hccparams1 >> 16;
	debug("xECP: %#x (%p)\n", xECP, mmiospace + (xECP * 4));
	iterate_ecps((u32*)mmiospace + xECP);

	// 4kB pages *must* be supported.
	assert(opregs[PAGESIZE] & 1);

	// Initialization - section 4.2 of xhci reference

	debug("Resetting...\n");
	opregs[USBCMD] = (opregs[USBCMD] & ~USBCMD_Run) | USBCMD_Reset;
	unsigned counter = 0;
	for (;;) {
		u32 status = opregs[USBSTS];
		u32 cmd = opregs[USBCMD];
		debug("Waiting for reset (%u): status %#x cmd %#x\n", counter++, status, cmd);
		if (status & USBSTS_NotReady) {
			debug("USB status: controller not ready\n");
		} else if (cmd & USBCMD_Reset) {
			debug("USB command: reset still set\n");
		} else if (!(status & USBSTS_Halted)) {
			debug("USB status: still running\n");
		} else {
			debug("Reset complete!\n");
			break;
		}
	}

	// 2. Program the Max Device Slots Enabled
	// TODO We probably need to allocate structures for these somewhere.
	opregs[CONFIG] = device_maxports;

	// 3. Program the Device Context Base Address Array Pointer
	uintptr_t dcbaaPhysAddr =
		(uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
			&device_context_addrs, 0, sizeof(device_context_addrs));
	opregs[DCBAAP] = dcbaaPhysAddr;
	opregs[DCBAAPH] = dcbaaPhysAddr >> 32;
	// The DCBAA has MaxSlotsEn+1 entries, entry 0 has the scratchpad entries.

	// 4. Define the Command Ring Dequeue Pointer by programming the Command
	// Ring Control Register (5.4.5) with a 64-bit address pointing to the
	// starting address of the first TRB of the Command Ring.
	uintptr_t page1p =
		(uintptr_t)map(0, MAP_DMA | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
			&page1, 0, sizeof(page1));
	uintptr_t commandRingPhys = page1p + offsetof(struct page1, command_ring);
	page1.command_ring[COMMAND_RING_SIZE - 1] =
		create_link_trb(commandRingPhys, TRB_ToggleC).cmd;
	u32 flags = opregs[CRCR] & 0x38;
	// The command ring should be stopped here.
	opregs[CRCR] = commandRingPhys | flags | CRCR_RingCycleState;
	opregs[CRCRH] = commandRingPhys >> 32;
	command_pcs = true;

	// 5. Initialize interrupters
	// The primary interrupter must be initialized before enabling Run, the
	// other interrupters may be ignored. I believe because they are not used
	// unless something is configured to use that interrupter for notifications.
	u32 erstsz = interruptregs[ERSTSZ];
	interruptregs[ERSTSZ] = (erstsz & ~0xffff) | 1;
	uintptr_t eventRingPhys = page1p + offsetof(struct page1, event_ring);
	page1.erst[0] = create_erse(eventRingPhys, EVENT_RING_SIZE);
	uintptr_t erstbaPhys = page1p + offsetof(struct page1, erst);
	*(volatile u64 *)(interruptregs + ERDP) = eventRingPhys;
	*(volatile u64 *)(interruptregs + ERSTBA) = erstbaPhys;
	// Enable the primary interrupter, and clear pending interrupts by writing
	// a 1 to the IP bit.
	interruptregs[IMAN] = IMAN_IntEnabled | IMAN_IntPending;
	// Disable moderation, just spam the interrupts please.
	interruptregs[IMOD] = 0;

	__barrier();

	// 6. Write the USBCMD (5.4.1) to turn the host controller ON via setting
	// the Run/Stop (R/S) bit to ‘1’. This operation allows the xHC to begin
	// accepting doorbell references.
	u32 cmd = opregs[USBCMD];
	cmd |= USBCMD_Run | USBCMD_IntEnable;
	opregs[USBCMD] = cmd;

	counter = 0;
	for (;;) {
		u32 status = opregs[USBSTS];
		u32 cmd = opregs[USBCMD];
		debug("Waiting for running state (%u): status %#x cmd %#x\n", counter++, status, cmd);
		if (status & USBSTS_NotReady) {
			debug("USB status: controller not ready\n");
		} else if (status & USBSTS_Halted) {
			debug("USB status: not running yet\n");
		} else {
			debug("Initialization complete!\n");
			break;
		}
	}

	for(;;) {
		uintptr_t rcpt = fresh;
		arg = 0;
		arg2 = 0;
		debug("receiving...\n");
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		debug("received %x from %x: %x %x\n", msg, rcpt, arg, arg2);
		if (rcpt == pin0_irq_handle && msg == MSG_PULSE) {
			// Disable all interrupts, then ACK receipt to PIC
			send1(MSG_IRQ_ACK, rcpt, arg);
			handle_irq();
			continue;
		}

		switch (msg & 0xff)
		{
		case MSG_PFAULT: {
			// TODO
			break;
		}
		}
	}
fail:
	abort();
}
