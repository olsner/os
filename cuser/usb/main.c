#include "common.h"

#define log(fmt, ...) printf("USB: " fmt, ## __VA_ARGS__)
#if 1
#define debug log
#else
#define debug(...) (void)0
#endif

// "main" USB driver - this talks to both host controllers and USB device
// drivers and manages communication between them.

static const uintptr_t xhci_handle = 7;
static const uintptr_t fresh = 0x100;
static const uintptr_t bus_handle_base = 0x200;
static uintptr_t bus_handle_max;

static uintptr_t bus_from_handle(uintptr_t h) {
	return h - bus_handle_base;
}
static uintptr_t handle_from_bus(uintptr_t b) {
	return bus_handle_base + b;
}

enum BMRequestType {
	ReqType_HostToDev = 0 << 7,
	ReqType_DevToHost = 1 << 7,
	ReqType_Standard = 0 << 5,
	ReqType_Class = 1 << 5,
	ReqType_Vendor = 2 << 5,
	// 3 << 5: Reserved
	ReqType_Device = 0,
	ReqType_Interface = 1,
	ReqType_EndPoint = 2,
	ReqType_Other = 3,
	// 4..31: Reserved
};
enum BMRequest {
	Req_GetStatus,
	Req_ClearFeature,
	// 2: reserved
	Req_SetFeature = 3,
	// 4: reserved
	Req_SetAddress = 5, // Not used - buses handle this
	Req_GetDescriptor
};

static void handle_bus_msg(const uintptr_t bus, uintptr_t msg, uintptr_t arg, uintptr_t arg2) {
	switch (msg & 0xff)
	{
	case MSG_USB_NEW_DEVICE: {
		u8 slot = arg;
		debug("New device, bus %u slot %u\n", bus_from_handle(bus), slot);
		// Fetch the first 8 bytes of the device descriptor before addressing
		// the device.
		usb_transfer_arg targ;
		targ.addr = slot;
		targ.ep = 0;
		targ.flags = UTF_ImmediateData | UTF_DirectionIn | UTF_SetupHasData;
		targ.type = UTT_ControlTransaction;
		targ.length = 8;
		arg = targ.i;
		struct usb_control_setup control_setup = {
			ReqType_DevToHost | ReqType_Standard | ReqType_Device,
			Req_GetDescriptor,
			1 << 8, 0, 8 };
		arg2 = *(u64*)&control_setup;
		log("Sending: transfer to %ld with %lx,%lx\n", bus_from_handle(bus), arg, arg2);
		msg = sendrcv2(MSG_USB_TRANSFER, bus, &arg, &arg2);
		log("Transfer reply: %lx with %lx\n", msg, arg2);
		// Parse something interesting out of the descriptor? (Or wait until
		// we have addressed it.)
		arg = slot;
		arg2 = 0;
		msg = sendrcv2(MSG_USB_ADDR_DEVICE, bus, &arg, &arg2);
		u8 addr = arg;
		log("Device addressed to %u.%u\n", bus_from_handle(bus), addr);
		break;
	}
	}
}

static void register_buses(uintptr_t rcpt, const size_t buses) {
	log("%x: %d buses: %d..%d\n", rcpt, buses, bus_from_handle(bus_handle_max), bus_from_handle(bus_handle_max) + buses - 1);
	for (uintptr_t n = 0; n < buses; n++) {
		const uintptr_t bus = bus_handle_max++;
		hmod_copy(rcpt, bus);
		send1(MSG_USB_CONTROLLER_INIT, bus, n);
	}
}

void start()
{
	__default_section_init();
	bus_handle_max = bus_handle_base;
	for (;;) {
		uintptr_t rcpt = fresh;
		uintptr_t arg = 0;
		uintptr_t arg2 = 0;
		debug("receiving...\n");
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		debug("received %x from %x: %x %x\n", msg, rcpt, arg, arg2);

		if (msg == MSG_USB_CONTROLLER_INIT) {
			register_buses(rcpt, arg);
		}
		if (rcpt >= bus_handle_base && rcpt < bus_handle_max) {
			handle_bus_msg(rcpt, msg, arg, arg2);
		}
	}
}
