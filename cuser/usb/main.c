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

static void handle_xhci_msg(uintptr_t msg, uintptr_t arg, uintptr_t arg2) {
	switch (msg & 0xff)
	{
	case MSG_USB_NEW_DEVICE: {
		debug("New device, slot %u\n", arg);
		break;
	}
	}
}

void start()
{
	__default_section_init();
	for (;;) {
		uintptr_t rcpt = fresh;
		uintptr_t arg = 0;
		uintptr_t arg2 = 0;
		debug("receiving...\n");
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		debug("received %x from %x: %x %x\n", msg, rcpt, arg, arg2);

		if (rcpt == xhci_handle) {
			handle_xhci_msg(msg, arg, arg2);
		}
	}
}
