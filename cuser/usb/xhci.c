#include "common.h"

#define log(fmt, ...) printf("xhci: " fmt, ## __VA_ARGS__)
#if 1
#define debug log
#else
#define debug(...) (void)0
#endif

static const uintptr_t usb_handle = 6;
static const uintptr_t acpi_handle = 4;
static const uintptr_t pic_handle = 2;
static const uintptr_t pin0_irq_handle = 0x100;
static const uintptr_t fresh = 0x101;
static const uintptr_t bus_handle_base = 0x200;

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
	HCCP1_AC64 = 1 << 0,
	// Bandwidth negotiation capability
	HCCP1_ContextSize = 1 << 2,
	HCCP1_PowerPowerControl = 1 << 3,
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
	PORT_NUMREGS = 4, // Number of dwords per port - 0x10 bytes => 4 dwords.
};
enum PSCRBits
{
	PSCR_CurrentConnectStatus = 1 << 0,
	PSCR_Enabled = 1 << 1,
	// 2: RsvdZ
	PSCR_OverCurrentActive = 1 << 3,
	PSCR_Reset = 1 << 4,
	// 5..8: link state
	PSCR_LinkStateShift = 5,
	PSCR_LinkStateMask = 0xf << PSCR_LinkStateShift,
	PSCR_PortPower = 1 << 9,
	// 10..13: port speed
	PSCR_PortSpeedShift = 10,
	PSCR_PortSpeedMask = 0xf << PSCR_PortSpeedShift,
	// 14..15: Port Indicator (LED color)
	PSCR_LinkStateWriteStrobe = 1 << 16,
	PSCR_ConnectStatusChange = 1 << 17,
	PSCR_ResetChange = 1 << 21,
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
enum ERDPBits
{
	// bits 0..2: Dequeue ERST Segment Index
	ERDP_EHB = 8,
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
	u64 parameter;
	u32 status;
	u32 control;
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

enum TRBType
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

	// 32..39: events
	TRB_TransferEvent = 32,
	TRB_CommandCompleted = 33,
	TRB_PortStatusChange = 34,
	// 35 = bandwidth request, 36 = doorbell event (for virtualization)
	TRB_HostControllerEvent = 37,
	TRB_DeviceNotification = 38,
	TRB_MFIndexWrap = 39,
};
typedef enum TRBType TRBType;
enum TRBFlags
{
	TRB_Cycle = 1,
	TRB_ToggleC = 2,
	TRB_Chain = 1 << 4,
	TRB_IOC = 1 << 5,
	// Block SetAddress Request
	TRB_BSR = 1 << 9,
};
enum CompletionCodes
{
	CC_Invalid = 0,
	CC_Success = 1,
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
typedef trb event_trb;
static TRBType trb_type(trb *trb_) {
	return (trb_->control >> 10) & 0x3f;
}

static union trb create_link_trb(u64 address, u8 flags) {
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

static u8 command_data[COMMAND_RING_SIZE];
static u8 command_enqueue = 0;
// The PCS bit to put into created TRBs
static bool command_pcs = true;

static u8 event_ring_dequeue;
static bool event_ring_pcs = true;

static volatile u64 device_context_addrs[256] PLACEHOLDER_SECTION ALIGN(4096);
// Bytes per context entry.
static u8 context_size;
typedef struct input_context {
	u32 drop;
	u32 add;
	u32 rsvd[5];
	u32 control;
} input_context;
_Static_assert(sizeof(input_context) == 32, "input context size doesn't match spec");
typedef struct slot_context {
	u32 route : 20;
	u32 speed : 4;
	u32 : 1;
	u32 mtt : 1;
	u32 hub : 1;
	u32 entries : 5;
	u16 exit_latency;
	u8 root_hub_port;
	u8 num_ports;
	u8 tt_hub_slot;
	u8 tt_port;
	u16 ttt : 2;
	u16 : 4;
	u16 interrupter : 10;
	u32 device_address : 8;
	u32 : 19;
	u32 slot_state : 5;
	u32 rsvdO[4];
} slot_context;
_Static_assert(sizeof(slot_context) == 32, "slot context size doesn't match spec");
typedef struct endpoint_context {
	// u32 #0
	u8 ep_state; // Really just 3 bits
	u8 mult : 2;
	u8 max_primary_streams : 5;
	u8 linear_stream_array : 1;
	u8 interval;
	u8 max_esit_payload_hi;
	// u32 #1
	u8 : 1;
	u8 error_count : 2;
	u8 type : 3;
	u8 : 1;
	u8 hid : 1;
	u8 max_burst_size;
	u16 max_packet_size;
	// u32 #2, #3
	u64 trdp;
	// u32 #4
	u16 avg_trb_length;
	u16 max_esit_payload_lo;
	// u32 5..8
	u32 rsvd[3];
} endpoint_context;
_Static_assert(sizeof(endpoint_context) == 32, "endpoint context size doesn't match spec");
enum EPType
{
	EP_NotValid = 0,
	EP_IsochOut,
	EP_BulkOut,
	EP_InterruptOut,
	EP_Control = 4,
	EP_IsochIn,
	EP_BulkIn,
	EP_InterruptIn,
};
enum TRDPBits
{
	TRDP_DCS = 1,
};

static u64 dma_map(const volatile void* addr, size_t size) {
	const enum prot flags = MAP_DMA | PROT_READ | PROT_WRITE | PROT_NO_CACHE;
	return (u64)map(0, flags, addr, 0, size);
}
#define dma_map(obj) dma_map(&(obj), sizeof(obj))

#define MAX_DMA_BUFFERS 16
static u8 dma_buffer_space[MAX_DMA_BUFFERS][4096] PLACEHOLDER_SECTION ALIGN(4096);
typedef struct dma_buffer {
	u64 phys; // 0 for unmapped buffers
} dma_buffer;
static dma_buffer dma_buffers[MAX_DMA_BUFFERS];
typedef struct dma_buffer_ref {
	u64 phys;
	u8 *virtual;
} dma_buffer_ref;
static dma_buffer_ref allocate_dma_buffer() {
	dma_buffer_ref res = { 0, NULL };
	for (unsigned i = 0; i < MAX_DMA_BUFFERS; i++) {
		dma_buffer *buf = dma_buffers + i;
		if (buf->phys & 1) continue;

		if (!buf->phys) {
			buf->phys = dma_map(dma_buffer_space[i]);
		}
		res.phys = buf->phys;
		res.virtual = dma_buffer_space[i];
		memset(res.virtual, 0, 4096);

		buf->phys |= 1;
		return res;
	}
	assert(!"Ran out of DMA buffers...");
	return res;
}
static void free_dma_buffer(u64 phys) {
	for (unsigned i = 0; i < MAX_DMA_BUFFERS; i++) {
		dma_buffer *buf = dma_buffers + i;
		if (buf->phys != (phys | 1)) continue;
		buf->phys ^= 1;
		return;
	}
}

enum EcpCapIds
{
	CAPID_SUPPORTED_PROTOCOL = 2,
};
static uint8_t maxport, numports;
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
			u8 portcount = ports >> 8;
			u8 firstport = ports;
			u8 lastport = firstport + portcount - 1;
			debug("%p: Supported protocol %s %x.%x, ports %u..%u\n", ecp,
				&nm, ecpr >> 24, (ecpr >> 16) & 0xff,
				firstport, lastport);
			if (lastport > maxport) {
				maxport = lastport;
			}
			numports += portcount;
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

static void print_pscr(u8 port, u32 pscr);

static bool enqueue_command2(struct command_trb *cmd, u8 data) {
	if (!!(page1.command_ring[command_enqueue].control & TRB_Cycle) ==
		command_pcs) {
		debug("Eww... Command ring is full.\n");
		return false;
	}
	page1.command_ring[command_enqueue] = *cmd;
	command_data[command_enqueue] = data;
	if (++command_enqueue == COMMAND_RING_SIZE) {
		debug("enqueue_command: command ring wrapped!\n");
		command_enqueue = 0;
		command_pcs ^= 1;
	}
	doorbells[0] = 0;
	return true;
}

// TODO warn_unused_result
static bool enqueue_command(struct command_trb *cmd, TRBType command, u8 data)
	__attribute__((nonnull));

static bool enqueue_command(struct command_trb *cmd, TRBType command, u8 data) {
	cmd->control |= (command << 10) | (command_pcs ? TRB_Cycle : 0);
	return enqueue_command2(cmd, data);
}

static void proceed_port(u8 port) {
	volatile u32* regs = opregs + PORTREGSET + (port - 1) * PORT_NUMREGS;
	u32 pscr = regs[PSCR];
	if (!(pscr & PSCR_PortPower)) {
		debug("port %d: not powered\n", port);
		return;
	}
	if (!(pscr & PSCR_CurrentConnectStatus) || (pscr & PSCR_Reset)) {
		return;
	}

	u8 linkState = (pscr & PSCR_LinkStateMask) >> PSCR_LinkStateShift;
	// Failed USB3 device. Ignore. (Would it help to reset the port here?)
	if (!(pscr & PSCR_Enabled) && linkState == 5) {
		return;
	}

	// USB3:
	// a) successful: enabled=1 reset=0 linkstate=0
	// b) unsuccessful: enabled=0 reset=0 linkstate=5 (RxDetect)
	// USB2:
	// 1. enabled=0 reset=0 linkstate=7 (Polling)
	// 2. reset it
	// 3. enabled=1 reset=0 linkstate=0 (U0)

	regs[PSCR] = PSCR_ResetChange | PSCR_ConnectStatusChange | PSCR_PortPower;
	print_pscr(port, pscr);

	if (pscr & PSCR_Enabled) {
		if (!(pscr & (PSCR_Reset | PSCR_LinkStateMask))) {
			debug("port %d: Connected!\n", port);
			// TODO Should copy the slot type from the Supported Protocol
			// capability for the port, 0 works only for USB2 and USB3.
			struct command_trb cmd = { 0, 0, 0 };
			enqueue_command(&cmd, TRB_CMD_EnableSlot, port);
		}
	} else {
		if (linkState == 7) {
			debug("port %d: USB2 device, resetting to proceed to Enabled\n", port);
			// Assume that when this is set, all other bits are ignored.
			// There are many bits in the PSCR that are e.g.
			// write-1-to-clear or similar...
			regs[PSCR] = PSCR_Reset;
		}
	}
}

#if 0
static void disable_slot(u8 slot) {
	// Do a TRB_CMD_DisableSlot
}
#endif

static slot_context *get_slot_ctx(void *p) {
	return (slot_context*)((u8*)p + context_size);
}
static endpoint_context *get_ep_ctx(void *p, unsigned ep) {
	return (endpoint_context*)((u8*)p + (ep + 2) * context_size);
}

static void address_device(u8 port, u8 slot, bool block_set) {
	debug("address device: port %d -> slot %d\n", port, slot);
	dma_buffer_ref input = allocate_dma_buffer();
	dma_buffer_ref devctx = allocate_dma_buffer();
	dma_buffer_ref ring = allocate_dma_buffer();
	device_context_addrs[slot] = devctx.phys;

	// Section 4.3.3 ...
	input_context* ic = (input_context*)input.virtual;
	ic->add = 3; // slot context and one endpoint context

	slot_context* sc = get_slot_ctx(ic);
	sc->root_hub_port = port;
	sc->route = 0;
	sc->entries = 1; // One, the control endpoint

	endpoint_context* ep = get_ep_ctx(ic, 0);
	ep->type = EP_Control;
	// TODO Check PORTSC Port Speed, set the appropriate max packet size for the control endpoint.
	ep->max_packet_size = 8;
	ep->max_burst_size = 0;
	ep->trdp = ring.phys | TRDP_DCS;
	ep->interval = 0;
	ep->max_primary_streams = 0;
	ep->mult = 0;
	ep->error_count = 3;

	trb* ring_trbs = (trb *)ring.virtual;
	ring_trbs[(4096 / sizeof(trb)) - 1] =
		create_link_trb(ring.phys, TRB_ToggleC);

	// TODO More barrier to make sure input changes are in RAM before we submit
	// the command.
	__barrier();

	command_trb cmd = { input.phys, 0, slot << 24 };
	if (block_set) {
		cmd.control |= TRB_BSR;
	}
	enqueue_command(&cmd, TRB_CMD_AddressDevice, port);
}

static void command_complete(u8 cmdpos, u8 ccode, u8 slot) {
	command_trb trb = page1.command_ring[cmdpos];
	u8 cmd = trb_type((union trb *)&trb);
	u8 data = command_data[cmdpos];
	if (ccode != CC_Success) {
		debug("Command %d failed: completion code %d\n", cmdpos, ccode);
	}
	switch (cmd) {
	case TRB_CMD_EnableSlot:
	{
		if (ccode == CC_Success) {
			address_device(data, slot, true);
		}
		break;
	}
	case TRB_CMD_AddressDevice:
	{
		u8 port = data;
		free_dma_buffer(trb.parameter);
		if (ccode == CC_Success) {
			// Device is addressed, now what?
			debug("AddressDevice completed! slot %d port %d ready to configure\n", slot, port);
			send1(MSG_USB_NEW_DEVICE, bus_handle_base + port, slot);
		} else {
			debug("AddressDevice failed! freeing slot %d port %d\n", slot, port);
			command_trb cmd = { 0, 0, 0 };
			enqueue_command(&cmd, TRB_CMD_DisableSlot, port);
		}
	}
	case TRB_CMD_DisableSlot:
	{
		u8 port = data;
		// TODO Find the transfer rings and free them.
		free_dma_buffer(device_context_addrs[slot]);
		device_context_addrs[slot] = 0;
		// More?
		// Disable the port that the device was connected on?
		break;
	}
	default:
		debug("Command %d completed, but I don't know what I did?", cmd);
		break;
	}
}

static void handle_event(event_trb* ev)
{
	u8 type = trb_type(ev);
	// Not all applicable to all events, but they are always in the same place.
	u8 ccode = ev->status >> 24;
	u32 cparam = ev->status & 0xffffff;
	u8 slot = ev->control >> 24;
	switch (type) {
	case TRB_PortStatusChange:
	{
		u8 port = ev->parameter >> 24;
		debug("Port status change: port=%d\n", port);
		proceed_port(port);
		break;
	}
	case TRB_CommandCompleted:
	{
		u8 cmdpos = (ev->parameter >> 4) & (COMMAND_RING_SIZE - 1);
		debug("Command completed: %d completion code=%d,param=%#x slot %d\n", cmdpos, ccode, cparam, slot);
		command_complete(cmdpos, ccode, slot);
		break;
	}
	default:
		debug("Unknown event %d\n", type);
		break;
	}
}

static void handle_irq(uintptr_t rcpt, uintptr_t arg)
{
	send1(MSG_IRQ_ACK, rcpt, arg);

	// For each interrupter (but since we're not MSI-X capable, there is only
	// one, the Primary Interrupter).
	if (interruptregs[IMAN] & IMAN_IntPending) {
		bool event = false;
		while (!!(page1.event_ring[event_ring_dequeue].control & TRB_Cycle) ==
			event_ring_pcs) {
			event = true;
			debug("Event at %d\n", event_ring_dequeue);
			handle_event(&page1.event_ring[event_ring_dequeue]);
			if (++event_ring_dequeue == EVENT_RING_SIZE) {
				debug("Event ring wrapped!\n");
				event_ring_pcs ^= 1;
				event_ring_dequeue = 0;
			}
		}
		volatile u64 *erdpp = (u64 *)(interruptregs + ERDP);
		if (event) {
			u64 erdp = *erdpp, prev_erdp = erdp;
			u8 new_erdp = event_ring_dequeue;
			erdp &= ~(0xf << 4);
			erdp |= (new_erdp << 4) | ERDP_EHB;
			debug("ERDP %#lx -> %#lx\n", prev_erdp, erdp);
			*erdpp = erdp;
		}
		interruptregs[IMAN] = IMAN_IntPending | IMAN_IntEnabled;
	}
}

static void print_pscr(u8 port, u32 pscr) {
	u8 linkState = (pscr >> PSCR_LinkStateShift) & 0xf;
	u8 portSpeed = (pscr >> PSCR_PortSpeedShift) & 0xf;
	u32 knownbits = PSCR_CurrentConnectStatus | PSCR_Reset |
		PSCR_ConnectStatusChange | PSCR_ResetChange | PSCR_Enabled |
		PSCR_PortPower | PSCR_LinkStateMask | PSCR_PortSpeedMask;
	debug("port %d: reset=%d resetchange=%d enabled=%d power=%d status=%d statuschange=%d\n",
			port, !!(pscr & PSCR_Reset), !!(pscr & PSCR_ResetChange),
			!!(pscr & PSCR_Enabled),
			!!(pscr & PSCR_PortPower),
			!!(pscr & PSCR_CurrentConnectStatus),
			!!(pscr & PSCR_ConnectStatusChange));
	debug("... linkState=%d portSpeed=%d other=%#x\n",
			linkState, portSpeed, pscr & ~knownbits);
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
	const u8 device_maxports = hcsparams1 & 0xff;
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
	context_size = (hccparams1 & HCCP1_ContextSize) ? 64 : 32;
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
	// FIXME Are these the same as the DCBAA entries? Then we can have 256.
	// I'm not sure if the number of ports is the appropriate value (= only
	// root-port-connected devices have slots), or if we'd rather enable all
	// the slots we can, so that we can support devices on hubs.
	opregs[CONFIG] = numports;

	// 3. Program the Device Context Base Address Array Pointer
	uintptr_t dcbaaPhysAddr = dma_map(device_context_addrs);
	opregs[DCBAAP] = dcbaaPhysAddr;
	opregs[DCBAAPH] = dcbaaPhysAddr >> 32;
	// The DCBAA has MaxSlotsEn+1 entries, entry 0 has the scratchpad entries.

	// 4. Define the Command Ring Dequeue Pointer by programming the Command
	// Ring Control Register (5.4.5) with a 64-bit address pointing to the
	// starting address of the first TRB of the Command Ring.
	uintptr_t page1p = dma_map(page1);
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

	// Ports are a bus each??
	debug("Sending controller init for %u buses...\n", maxport);
	send1(MSG_USB_CONTROLLER_INIT, usb_handle, maxport);
	debug("Sent controller init\n");

	for(;;) {
		uintptr_t rcpt = fresh;
		arg = 0;
		arg2 = 0;
		debug("receiving...\n");
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		debug("received %x from %x: %x %x\n", msg, rcpt, arg, arg2);
		if (rcpt == pin0_irq_handle && msg == MSG_PULSE) {
			handle_irq(rcpt, arg);
			continue;
		}
		if (rcpt == fresh && (msg & 0xff) == MSG_USB_CONTROLLER_INIT) {
			uintptr_t bus = arg;
			hmod_rename(rcpt, bus_handle_base + bus);
			proceed_port(bus);
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
