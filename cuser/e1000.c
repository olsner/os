#include "common.h"

static const uintptr_t acpi_handle = 4;
static const uintptr_t pic_handle = 2;
static const uintptr_t pin0_irq_handle = 0x100;

static volatile u32 mmiospace[128 * 1024 / 4] PLACEHOLDER_SECTION;

enum regs
{
	CTRL = 0,
	STATUS = 2,
	RCTL = 0x40,
};

u32 readpci32(u32 addr, u8 reg)
{
	uintptr_t arg = addr << 8 | (reg & 0xfc);
	sendrcv1(MSG_ACPI_READ_PCI, acpi_handle, &arg);
	return arg;
}

void start() {
	uintptr_t dummy = 0;
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
	if ((bar0 >> 1) & 3 == 2) // 64-bit
	{
		uintptr_t bar1 = readpci32(pci_id, PCI_BAR_1);
		printf("BAR0 was 64-bit, adding %x:%x.\n", bar1, mmiobase);
		mmiobase |= bar1 << 32;
	}
	// TODO Map unprefetchable!
	map(0, PROT_READ | PROT_WRITE, mmiospace, mmiobase, sizeof(mmiospace));

	u32 status = mmiospace[STATUS];
	printf("Status: %x\n", status);
	mmiospace[RCTL] = 2;
	printf("Status now: %x\n", mmiospace[STATUS]);
	// Read out BAR registers to get access to registers
	// Set some stuff: Set Link Up, Auto Speed Detect Enable
	// Clear: PHY Reset, VME (VLAN Enable)
	// Allocate receive descriptors and buffers
	//
	// (!) We need their physical addresses too...

fail:
	for(;;) {
		uintptr_t rcpt = 0;
		arg = 0;
		arg2 = 0;
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		printf("e1000: received %x from %x: %x %x\n", msg, rcpt, arg, arg2);
		switch (msg)
		{
		}
	}
}
