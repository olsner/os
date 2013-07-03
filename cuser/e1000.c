#include "common.h"

static const uintptr_t acpi_handle = 4;
static const uintptr_t pic_handle = 2;
static const uintptr_t pin0_irq_handle = 0x100;

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
