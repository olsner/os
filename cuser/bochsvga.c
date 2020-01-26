#include <assert.h>

#include "common.h"
#include "msg_acpi.h"
#include "msg_fb.h"

#define log printf
#if 0
#define debug log
#else
#define debug(...) (void)0
#endif

#define VBE_DISPI_MAX_XRES               2560
#define VBE_DISPI_MAX_YRES               1600
#define VBE_DISPI_MAX_BPP                32

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

enum Index {
	INDEX_ID = 0x0,
	INDEX_XRES = 0x1,
	INDEX_YRES = 0x2,
	INDEX_BPP = 0x3,
	INDEX_ENABLE = 0x4,
	INDEX_BANK = 0x5,
	INDEX_VIRT_WIDTH = 0x6,
	INDEX_VIRT_HEIGHT = 0x7,
	INDEX_X_OFFSET = 0x8,
	INDEX_Y_OFFSET = 0x9,
	INDEX_VIDEO_MEMORY_64K = 0xa,
};

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_BPP_4                  0x04
#define VBE_DISPI_BPP_8                  0x08
#define VBE_DISPI_BPP_15                 0x0F
#define VBE_DISPI_BPP_16                 0x10
#define VBE_DISPI_BPP_24                 0x18
#define VBE_DISPI_BPP_32                 0x20

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

static const uintptr_t acpi_handle = 6;
static const uintptr_t the_client = 0xff;
static const uintptr_t fresh = 0x100;

#define LFB_SIZE (16 * 1048576)

static u8 mmiospace[LFB_SIZE] PLACEHOLDER_SECTION;

static void outb(u16 port, u8 data) {
	portio(port, 0x11, data);
}

static u16 read_reg(enum Index i) {
	portio(VBE_DISPI_IOPORT_INDEX, 0x12, (u16)i);
	return portio(VBE_DISPI_IOPORT_DATA, 0x2, 0);
}

static void write_reg(enum Index i, u16 data) {
	portio(VBE_DISPI_IOPORT_INDEX, 0x12, (u16)i);
	portio(VBE_DISPI_IOPORT_DATA, 0x12, data);
}

static u32 readpci32(u32 addr, u8 reg)
{
	ipc_arg_t arg = addr << 8 | (reg & 0xfc);
	sendrcv1(MSG_ACPI_READ_PCI, acpi_handle, &arg);
	return arg;
}

void start() {
	__default_section_init();

	ipc_arg_t arg = 0x12341111; // Silly PCI ID of Bochs VGA 1234:1111
	log("bochsvga: looking for PCI device...\n");
	sendrcv1(MSG_ACPI_FIND_PCI, acpi_handle, &arg);
	if (arg == ACPI_PCI_NOT_FOUND)
	{
		log("bochsvga: No devices found\n");
		abort();
	}
	log("bochsvga: found %x\n", arg);
	// bus << 8 | dev << 3 | func
	const uintptr_t pci_id = arg;
	ipc_arg_t arg2 = 0; // No interrupts to claim.
	sendrcv2(MSG_ACPI_CLAIM_PCI, acpi_handle, &arg, &arg2);
	if (!arg)
	{
		log("bochsvga: failed :(\n");
		abort();
	}

	u32 bar0 = readpci32(pci_id, PCI_BAR_0);
	debug("BAR 0: %s %s %s: %x\n",
		bar0 & 1 ? "io" : "mem",
		(bar0 >> 1) & 3 ? "64-bit" : "32-bit",
		(bar0 >> 3) & 1 ? "prefetchable" : "non-prefetchable",
		bar0 & ~0xf);
	u64 mmiobase = bar0 & ~0xf;
	if (((bar0 >> 1) & 3) == 2) // 64-bit
	{
		u64 bar1 = readpci32(pci_id, PCI_BAR_1);
		debug("BAR0 was 64-bit, adding %lx:%lx.\n", bar1, mmiobase);
		mmiobase |= bar1 << 32;
	}
	debug("Mapping mmiospace %p to BAR %p\n", (void*)mmiospace, mmiobase);
	// TODO Map unprefetchable!
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE,
		(void*)mmiospace, mmiobase, sizeof(mmiospace));
	memset(mmiospace, 0, sizeof(mmiospace));

	u16 bochs_id = read_reg(INDEX_ID);
	debug("bochsvga: Found bochs version %x\n", bochs_id);
	assert(bochs_id >= VBE_DISPI_ID0 && bochs_id <= VBE_DISPI_ID5);

	for(;;) {
		ipc_dest_t rcpt = fresh;
		arg = 0;
		arg2 = 0;
		ipc_msg_t msg = recv2(&rcpt, &arg, &arg2);
		debug("bochsvga: received %x from %x: %x %x\n", msg, rcpt, arg, arg2);

		switch (msg & 0xff) {
		case MSG_SET_VIDMODE:
		{
			u32 w = arg >> 32;
			u32 h = arg;
			u32 bpp = arg2;
			log("bochsvga: %x setting video mode to %ux%u %ubpp\n", rcpt, w, h, bpp);
			assert(w <= VBE_DISPI_MAX_XRES && h <= VBE_DISPI_MAX_YRES);
			assert(bpp <= VBE_DISPI_MAX_BPP);
			write_reg(INDEX_ENABLE, VBE_DISPI_DISABLED);
			write_reg(INDEX_XRES, w);
			write_reg(INDEX_YRES, h);
			write_reg(INDEX_BPP, bpp);
			write_reg(INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED
				| VBE_DISPI_8BIT_DAC);
			debug("bochsvga: mode updated!\n");
			send2(MSG_SET_VIDMODE, rcpt, arg, arg2);
			hmod_rename(rcpt, the_client);
			break;
		}
		case MSG_SET_PALETTE:
		{
			u8 c = arg >> 24;
			u8 r = arg >> 16;
			u8 g = arg >> 8;
			u8 b = arg;
			outb(0x3c8, c);
			outb(0x3c9, r);
			outb(0x3c9, g);
			outb(0x3c9, b);
			debug("bochsvga: palette %x := (%x,%x,%x)\n", c, r, g, b);
			break;
		}
		case SYS_PFAULT:
		{
			assert(arg < LFB_SIZE && rcpt == the_client);
			if (arg < LFB_SIZE) {
				void *addr = (char*)&mmiospace + arg;
				int prot = arg2 & (PROT_READ | PROT_WRITE);
				debug("bochsvga: granting %p (%x) to client %x\n", addr, prot, rcpt);
				grant(rcpt, addr, prot);
			}
			break;
		}
		default:
			abort();
		}
	}
}
