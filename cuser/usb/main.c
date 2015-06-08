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

static void handle_bus_msg(uintptr_t bus, uintptr_t msg, uintptr_t arg, uintptr_t arg2) {
	switch (msg & 0xff)
	{
	case MSG_USB_NEW_DEVICE: {
		debug("New device, bus %u slot %u\n", bus_from_handle(bus), arg);
		// TODO: Get (8 bytes of) descriptor 0, then address the device
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
