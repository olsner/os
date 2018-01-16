#include <assert.h>
#include <stdlib.h>

#include "common.h"
#include "acpi.h"
#include "acpica.h"

static const uintptr_t pic_handle = 2;
static const uintptr_t ioapic_handle = 5;

// All controllers follow the REG_IRQ/ACK_IRQ interface, and should be
// duplicated for each interrupt they handle.
typedef struct irq_controller
{
	// Uses handles &GSI_HANDLES[gsi_base..first_gsi + count]
	u16 first_gsi;
	u16 last_gsi;
	// Base handle to copy when registering IRQ
	uintptr_t handle;
} irq_controller;
#define MAX_CONTROLLERS 1
static u8 n_controllers;
static irq_controller irq_controllers[MAX_CONTROLLERS];
#define MAX_GSI 4096
static const char GSI_INPUTS[MAX_GSI] ALIGN(4096) PLACEHOLDER_SECTION;
// Set to 1 when registered
static char GSI_OUTPUTS[MAX_GSI];

// Only for interrupts registered to ACPI itself.
typedef struct irq_reg
{
	UINT32 InterruptNumber;
	ACPI_OSD_HANDLER ServiceRoutine;
	void* Context;
	struct irq_reg* Next;
} irq_reg;

static irq_reg* irq_regs;

UINT32
AcpiOsInstallInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
	printf("AcpiOsInstallInterruptHandler %#x\n", InterruptNumber);
	irq_reg* reg = malloc(sizeof(irq_reg));
	reg->InterruptNumber = InterruptNumber;
	reg->ServiceRoutine = ServiceRoutine;
	reg->Context = Context;
	reg->Next = irq_regs;
	irq_regs = reg;

	printf("Registered IRQ %#x to %p/%p\n", InterruptNumber, ServiceRoutine, Context);
	return (AE_OK);
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
	printf("AcpiOsRemoveInterruptHandler %#x\n", InterruptNumber);
    return (AE_OK);
}

static void HandleIrq(const irq_reg* irq, uintptr_t num) {
	printf("acpica: IRQ %#lx: Calling %p/%p (registered for %#x)\n", num,
			irq->ServiceRoutine, irq->Context, irq->InterruptNumber);
	irq->ServiceRoutine(irq->Context);
}

int AcpiOsCheckInterrupt(uintptr_t rcpt, uintptr_t arg)
{
	if (rcpt >= (uintptr_t)&GSI_INPUTS[0]
		&& rcpt < (uintptr_t)&GSI_INPUTS[MAX_GSI])
	{
		unsigned gsi = rcpt - (uintptr_t)&GSI_INPUTS[0];
		assert(gsi < MAX_GSI);
		if (GSI_OUTPUTS[gsi]) {
			pulse((uintptr_t)&GSI_OUTPUTS[gsi], 1);
			return 1;
		}
		const irq_reg* irq = irq_regs;
		while (irq) {
			if (irq->InterruptNumber == gsi) {
				HandleIrq(irq, gsi);
				send0(MSG_IRQ_ACK, rcpt);
				return 1;
			}
			irq = irq->Next;
		}
		printf("GSI %d unregistered\n", gsi);
		return 1;
	}
	printf("IRQ %#lx/%#x: Not found!\n", rcpt, arg);
	return 0;
}

void AckIRQ(uintptr_t rcpt) {
	if (rcpt >= (uintptr_t)&GSI_OUTPUTS[0]
		&& rcpt < (uintptr_t)&GSI_OUTPUTS[MAX_GSI])
	{
		unsigned gsi = rcpt - (uintptr_t)&GSI_OUTPUTS[0];
//		printf("Sending ack for GSI %d to %p\n", gsi, &GSI_INPUTS[gsi]);
		send0(MSG_IRQ_ACK, (uintptr_t)&GSI_INPUTS[gsi]);
	}
}

static irq_controller *ControllerForGSI(uintptr_t gsi) {
	for (int i = 0; i < n_controllers; i++) {
		irq_controller *p = &irq_controllers[i];
		if (p->first_gsi <= gsi && gsi <= p->last_gsi) {
			return p;
		}
	}
	return NULL;
}

void RegIRQ(uintptr_t rcpt, uintptr_t int_spec)
{
	unsigned gsi = int_spec & 0xff;

	printf("acpica: Registering Interrupt %#x to %#lx\n", gsi, rcpt);
	assert(gsi < MAX_GSI);
	assert(!GSI_OUTPUTS[gsi]);

	GSI_OUTPUTS[gsi] = 1;
	// FIXME (if IOAPIC) Send flags with polarity and edge/level trigger
	send1(MSG_REG_IRQ, rcpt, gsi);
	hmod_rename(rcpt, (uintptr_t)&GSI_OUTPUTS[gsi]);

	irq_controller *p = ControllerForGSI(gsi);
	if (!p) {
		printf("acpica: No controller for GSI %d\n", gsi);
		return;
	}

	ipc_dest_t h = (ipc_dest_t)&GSI_INPUTS[gsi];
	hmod_copy(p->handle, h);
	ipc_arg_t arg = int_spec;
	sendrcv1(MSG_REG_IRQ, h, &arg);
	printf("acpica: Registered GSI %d through %#x\n", gsi, p->handle);
}

static void add_irq_controller(uintptr_t handle, u32 gsi_base, u32 count)
{
	assert(n_controllers < MAX_CONTROLLERS);
	irq_controller *p = &irq_controllers[n_controllers++];
	assert(gsi_base < MAX_GSI);
	assert(gsi_base + count < MAX_GSI);
	p->first_gsi = gsi_base;
	p->last_gsi = gsi_base + count - 1;
	p->handle = handle;

	printf("Registered GSIs %d..%d to interrupt controller %#x\n",
		gsi_base, p->last_gsi, handle);
}

void AddIOAPIC(ACPI_MADT_IO_APIC *apic)
{
	ipc_arg_t arg1 = apic->Id, arg2 = apic->Address, arg3 = apic->GlobalIrqBase;
	sendrcv3(MSG_ACPI_ADD_IOAPIC, ioapic_handle, &arg1, &arg2, &arg3);
	if (!arg1) {
		printf("IOAPIC Initialization failed!\n");
		abort();
	} else {
		printf("IOAPIC Initialization successful: %d interrupts\n", arg1);
	}
	add_irq_controller(ioapic_handle, apic->GlobalIrqBase, arg1);
}

void AddPIC()
{
	add_irq_controller(pic_handle, 0, 16);
}
