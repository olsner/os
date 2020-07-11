#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "acpi.h"
#include "acpica.h"

static const uintptr_t pic_handle = 2;
static const uintptr_t ioapic_handle = 5;

static const bool log_acpica_interrupts = false;
static const bool log_irq = false;
static const bool log_ioapic_init = true;
static const bool log_apic_table = false;
static const bool log_route_irq = false;

// All controllers follow the REG_IRQ/ACK_IRQ interface, and should be
// duplicated for each interrupt they handle.
typedef struct irq_controller
{
	u16 first_gsi;
	u16 last_gsi;
	// file descriptor for the controller, used to add more IRQs
	int controller;
} irq_controller;
#define MAX_CONTROLLERS 1
static u8 n_controllers;
static irq_controller irq_controllers[MAX_CONTROLLERS];

#define MAX_GSI 64
// fd for each GSI registered with an upstream interrupt controller.
static int upstream_gsi_fd[MAX_GSI];
// fd for each downstream GSI client registered to us.
static int downstream_gsi_fd[MAX_GSI];

static int find_fd(const int fd, const int* array, int n) {
	for (int i = 0; i < n; i++) {
		if (array[i] == fd) {
			return i;
		}
	}
	return -1;
}
static int find_upstream_gsi(int fd) {
	return find_fd(fd, upstream_gsi_fd, MAX_GSI);
}
static int find_downstream_gsi(int fd) {
	return find_fd(fd, downstream_gsi_fd, MAX_GSI);
}

// Only for interrupts registered to ACPI itself.
typedef struct irq_reg
{
	int InterruptNumber;
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
	log(acpica_interrupts, "AcpiOsInstallInterruptHandler %#x\n", InterruptNumber);
	irq_reg* reg = malloc(sizeof(irq_reg));
	reg->InterruptNumber = InterruptNumber;
	reg->ServiceRoutine = ServiceRoutine;
	reg->Context = Context;
	reg->Next = irq_regs;
	irq_regs = reg;

	log(acpica_interrupts, "Registered IRQ %#x to %p/%p\n", InterruptNumber, ServiceRoutine, Context);
	return (AE_OK);
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
	log(acpica_interrupts, "AcpiOsRemoveInterruptHandler %#x\n", InterruptNumber);
    return (AE_OK);
}

static void HandleIrq(const irq_reg* irq, uintptr_t num) {
	log(acpica_interrupts,
            "IRQ %#lx: Calling %p/%p (registered for %#x)\n", num,
			irq->ServiceRoutine, irq->Context, irq->InterruptNumber);
	irq->ServiceRoutine(irq->Context);
}

bool AcpiOsCheckInterrupt(ipc_dest_t rcpt, uintptr_t arg)
{
	int gsi = find_upstream_gsi(msg_dest_fd(rcpt));
	if (gsi >= 0) {
		if (downstream_gsi_fd[gsi] >= 0) {
			log(irq, "Forwarding GSI %d to downstream %d\n", gsi, downstream_gsi_fd[gsi]);
			pulse(downstream_gsi_fd[gsi], 1);
			return true;
		}
		const irq_reg* irq = irq_regs;
		while (irq) {
			if (irq->InterruptNumber == gsi) {
				HandleIrq(irq, gsi);
				send0(MSG_IRQ_ACK, rcpt);
				return true;
			}
			irq = irq->Next;
		}
		log(irq, "GSI %d unregistered\n", gsi);
		return 1;
	}
	log(irq, "IRQ %#lx/%#x: Not found! (gsi=%d)\n", rcpt, arg, gsi);
	return false;
}

void AckIRQ(ipc_dest_t rcpt) {
	int gsi = find_downstream_gsi(msg_dest_fd(rcpt));
	if (gsi >= 0) {
		assert(upstream_gsi_fd[gsi] >= 0);
//		log(irq, "Sending ack for GSI %d to %p\n", gsi, &GSI_INPUTS[gsi]);
		send0(MSG_IRQ_ACK, upstream_gsi_fd[gsi]);
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

	log(irq, "Registering Interrupt %d to %#lx\n", gsi, rcpt);
	assert(gsi < MAX_GSI);
	assert(downstream_gsi_fd[gsi] < 0);

	int fds[2];
	if (rcpt & MSG_TX_ACCEPTFD) {
		socketpair(fds);
		rcpt |= MSG_TX_CLOSEFD;
	}
	else {
		fds[0] = msg_dest_fd(rcpt);
		fds[1] = gsi;
	}

	downstream_gsi_fd[gsi] = fds[0];
	log(irq, "Registering GSI %d: sending downstream fd %d to %#lx (our end %d)\n", gsi, fds[1], rcpt, fds[0]);
	send1(MSG_REG_IRQ, rcpt, fds[1]);

	irq_controller *p = ControllerForGSI(gsi);
	if (!p) {
		log(irq, "No controller for GSI %d\n", gsi);
		return;
	}

	// TODO If sharing interrupts, we might have already registered the upstream IRQ.
	assert(upstream_gsi_fd[gsi] < 0);
	// FIXME (if IOAPIC) Send flags with polarity and edge/level trigger
	ipc_arg_t arg = int_spec;
	sendrcv1(MSG_REG_IRQ, MSG_TX_ACCEPTFD | p->controller, &arg);
	upstream_gsi_fd[gsi] = arg;
	log(irq, "Registered GSI %d: upstream fd %d through %d\n", gsi, upstream_gsi_fd[gsi], p->controller);
	log(irq, "Registered GSI %d: downstream fd %d\n", gsi, downstream_gsi_fd[gsi]);
}

static void add_irq_controller(uintptr_t handle, u32 gsi_base, u32 count)
{
	assert(n_controllers < MAX_CONTROLLERS);
	irq_controller *p = &irq_controllers[n_controllers++];
	assert(gsi_base < MAX_GSI);
	assert(gsi_base + count < MAX_GSI);
	p->first_gsi = gsi_base;
	p->last_gsi = gsi_base + count - 1;
	p->controller = handle;

	log(irq, "Registered GSIs %d..%d to interrupt controller %#x\n",
		gsi_base, p->last_gsi, handle);
}

static bool AddIOAPIC(ACPI_MADT_IO_APIC *apic)
{
	ipc_arg_t arg1 = apic->Id, arg2 = apic->Address, arg3 = apic->GlobalIrqBase;
	sendrcv3(MSG_ACPI_ADD_IOAPIC, ioapic_handle, &arg1, &arg2, &arg3);
	if (!arg1) {
		printf("IOAPIC Initialization failed!\n");
		return false;
	} else {
		log(ioapic_init, "IOAPIC Initialization successful: %d interrupts\n", arg1);
	}
	add_irq_controller(ioapic_handle, apic->GlobalIrqBase, arg1);
	return true;
}

#pragma pack(1)
typedef union acpi_apic_struct
{
	struct {
		UINT8 Type;
		UINT8 Length;
	};
	ACPI_MADT_LOCAL_APIC LocalApic;
	ACPI_MADT_IO_APIC IOApic;
	ACPI_MADT_INTERRUPT_OVERRIDE InterruptOverride;
	ACPI_MADT_LOCAL_APIC_NMI LocalApicNMI;
} ACPI_APIC_STRUCT;
#pragma pack()

ACPI_STATUS FindIOAPICs(int *pic_mode) {
	// Initialize to -1.
	memset(&upstream_gsi_fd, 0xff, sizeof(upstream_gsi_fd));
	memset(&downstream_gsi_fd, 0xff, sizeof(downstream_gsi_fd));

	ACPI_TABLE_MADT* table = NULL;
	ACPI_STATUS status = AcpiGetTable("APIC", 0, (ACPI_TABLE_HEADER**)&table);
	CHECK_STATUS("AcpiGetTable");

	bool ioapic_found = false;
	bool ioapic_failed = false;
	char* endOfTable = (char*)table + table->Header.Length;
	char* p = (char*)(table + 1);
	int n = 0;
	while (p < endOfTable) {
		ACPI_APIC_STRUCT* apic = (ACPI_APIC_STRUCT*)p;
		p += apic->Length;
		n++;
		switch (apic->Type)
		{
		case ACPI_MADT_TYPE_IO_APIC:
			log(ioapic_init, "Found I/O APIC. ID %#x Addr %#x GSI base %#x.\n",
				(int)apic->IOApic.Id,
				apic->IOApic.Address,
				apic->IOApic.GlobalIrqBase);
			ioapic_found = true;
			ioapic_failed = !AddIOAPIC(&apic->IOApic);
			break;
		}
	}
	// Note if there were multiple I/O APICs but one of them failed init, we
	// might need to deinitialize(?) it for PIC mode to be safe again.
	if (ioapic_found && !ioapic_failed) {
		*pic_mode = 1;
		log(ioapic_init, "I/O APICs found, setting APIC mode\n");
	} else {
		*pic_mode = 0;
		log(ioapic_init, "I/O APICs failed or not found, setting PIC mode\n");
		AddPIC();
	}
failed:
	return AE_OK;
}

void AddPIC()
{
	add_irq_controller(pic_handle, 0, 16);
}

ACPI_STATUS PrintAPICTable(void) {
	static const char *polarities[] = {
		"Bus-Conformant",
		"Active-High",
		"Reserved",
		"Active-Low"
	};
	static const char *triggerings[] = {
		"Bus-Conformant",
		"Edge-Triggered",
		"Reserved",
		"Level-Triggered"
	};

	ACPI_TABLE_MADT* table = NULL;
	ACPI_STATUS status = AcpiGetTable("APIC", 0, (ACPI_TABLE_HEADER**)&table);
	CHECK_STATUS("AcpiGetTable");

	log(apic_table, "Found APIC table: %p\n", table);
	log(apic_table, "Address of Local APIC: %#x\n", table->Address);
	log(apic_table, "Flags: %#x\n", table->Flags);
	char* endOfTable = (char*)table + table->Header.Length;
	char* p = (char*)(table + 1);
	int n = 0;
	while (p < endOfTable) {
		ACPI_APIC_STRUCT* apic = (ACPI_APIC_STRUCT*)p;
		p += apic->Length;
		n++;
		switch (apic->Type)
		{
		case ACPI_MADT_TYPE_LOCAL_APIC:
			log(apic_table, "%d: Local APIC. Processor ID %#x APIC ID %#x En=%d (%#x)\n", n,
				(int)apic->LocalApic.ProcessorId,
				(int)apic->LocalApic.Id,
				apic->LocalApic.LapicFlags & 1,
				apic->LocalApic.LapicFlags);
			break;
		case ACPI_MADT_TYPE_IO_APIC:
			log(apic_table, "%d: I/O APIC. ID %#x Addr %#x GSI base %#x\n", n,
				(int)apic->IOApic.Id,
				apic->IOApic.Address,
				apic->IOApic.GlobalIrqBase);
			break;
		case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		{
			UINT32 flags = apic->InterruptOverride.IntiFlags;
			log(apic_table, "%d: Interrupt Override. Source %#x GSI %#x Pol=%s Trigger=%s\n", n,
				apic->InterruptOverride.SourceIrq,
				apic->InterruptOverride.GlobalIrq,
				polarities[flags & 3], triggerings[(flags >> 2) & 3]);
			break;
		}
		case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		{
			UINT32 flags = apic->InterruptOverride.IntiFlags;
			log(apic_table, "%d: Local APIC NMI. Processor ID %#x Pol=%s Trigger=%s LINT# %#x\n", n,
				apic->LocalApicNMI.ProcessorId,
				polarities[flags & 3], triggerings[(flags >> 2) & 3],
				apic->LocalApicNMI.Lint);
			break;
		}
		default:
			log(apic_table, "%d: Unknown APIC type %d\n", n, apic->Type);
			break;
		}
	}

failed:
	return status;
}

typedef struct IRQRouteData
{
	ACPI_PCI_ID pci;
	unsigned pin;
	int8_t gsi;
	// triggering: 1 = edge triggered, 0 = level
	int8_t triggering;
	// polarity: 1 = active-low, 0 = active-high
	int8_t polarity;
	BOOLEAN found;
} IRQRouteData;


static ACPI_STATUS RouteIRQLinkDevice(ACPI_HANDLE Device, ACPI_PCI_ROUTING_TABLE* found, IRQRouteData* data) {
	ACPI_STATUS status = AE_OK;
	ACPI_HANDLE LinkDevice = NULL;
	ACPI_BUFFER buffer = {0, NULL};

	log(route_irq, "Routing IRQ Link device %s\n", found->Source);
	status = AcpiGetHandle(Device, found->Source, &LinkDevice);
	CHECK_STATUS("AcpiGetHandle %s", found->Source);

	ResetBuffer(&buffer);
	status = AcpiGetCurrentResources(LinkDevice, &buffer);
	CHECK_STATUS("AcpiGetCurrentResources");
	//log(route_irq, "Got %lu bytes of current resources\n", buffer.Length);
	ACPI_RESOURCE* resource = (ACPI_RESOURCE*)buffer.Pointer;
	switch (resource->Type) {
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		// The interrupt count must be 1 when returned from _CRS, supposedly.
		// I think the "possible resource setting" may list several.
		log(route_irq, "Extended IRQ: %d interrupts, first one %#x. %s triggered, Active-%s.\n",
			resource->Data.ExtendedIrq.InterruptCount,
			resource->Data.ExtendedIrq.Interrupts[0],
			resource->Data.ExtendedIrq.Triggering ? "Edge" : "Level",
			resource->Data.ExtendedIrq.Polarity ? "Low" : "High");
		data->gsi = resource->Data.ExtendedIrq.Interrupts[0];
		data->triggering = resource->Data.ExtendedIrq.Triggering;
		data->polarity = resource->Data.ExtendedIrq.Polarity;
		break;
	case ACPI_RESOURCE_TYPE_IRQ:
		log(route_irq, "IRQ: %d interrupts, first one %#x.\n",
			resource->Data.Irq.InterruptCount,
			resource->Data.Irq.Interrupts[0]);
		data->gsi = resource->Data.Irq.Interrupts[0];
		// PIC interrupts can't be set up for specific polarity and triggering,
		// I think.
		break;
	default:
		log(route_irq, "RouteIRQLinkDevice: unknown resource type %d\n", resource->Type);
		status = AE_BAD_DATA;
		goto failed;
	}
	status = AcpiSetCurrentResources(LinkDevice, &buffer);
	CHECK_STATUS("AcpiSetCurrentResources");

failed:
	FreeBuffer(&buffer);
	return_ACPI_STATUS(status);
}

static ACPI_STATUS RouteIRQCallback(ACPI_HANDLE Device, UINT32 Depth, void *Context, void** ReturnValue)
{
	IRQRouteData* data = (IRQRouteData*)Context;
	ACPI_STATUS status = AE_OK;
	ACPI_RESOURCE* resource = NULL;
	ACPI_BUFFER buffer = {0, NULL};
	buffer.Length = ACPI_ALLOCATE_BUFFER;
	ACPI_PCI_ROUTING_TABLE* found = NULL;

	ACPI_DEVICE_INFO* info = NULL;
	status = AcpiGetObjectInfo(Device, &info);
	CHECK_STATUS("AcpiGetObjectInfo");

	if (!(info->Flags & ACPI_PCI_ROOT_BRIDGE)) {
		log(route_irq, "RouteIRQCallback: not a root bridge.\n");
		goto failed;
	}

	log(route_irq, "RouteIRQ: Root bridge with address %#x:\n", info->Address);
	int rootBus = -1;

	// Get _CRS, parse, check if the bus number range includes the one in
	// data->pci.Bus - then we've found the right *root* PCI bridge.
	// Though this might actually be a lot more complicated if we allow for
	// multiple root pci bridges.
	status = AcpiGetCurrentResources(Device, &buffer);
	CHECK_STATUS("AcpiGetCurrentResources");
	//log(route_irq, "Got %lu bytes of current resources\n", buffer.Length);
	//status = AcpiBufferToResource(buffer.Pointer, buffer.Length, &resource);
	resource = (ACPI_RESOURCE*)buffer.Pointer;
	//log(route_irq, "Got resources %p (status %#x)\n", resource, status);
	//CHECK_STATUS();
	while (resource->Type != ACPI_RESOURCE_TYPE_END_TAG) {
		//log(route_irq, "Got resource type %d\n", resource->Type);
		ACPI_RESOURCE_ADDRESS64 addr64;
		ACPI_STATUS status = AcpiResourceToAddress64(resource, &addr64);
		if (status == AE_OK && addr64.ResourceType == ACPI_BUS_NUMBER_RANGE)
		{
			log(route_irq, "RouteIRQ: Root bridge bus range %#x..%#x\n",
					addr64.Address.Minimum,
					addr64.Address.Maximum);
			if (data->pci.Bus < addr64.Address.Minimum ||
				data->pci.Bus > addr64.Address.Maximum)
			{
				// This is not the root bridge we're looking for...
				goto failed;
			}
			rootBus = addr64.Address.Minimum;
			break;
		}
		resource = ACPI_NEXT_RESOURCE(resource);
	}
	// dunno!
	if (rootBus == -1)
	{
		log(route_irq, "Couldn't figure out the bus number for root bridge %#x\n",
				info->Address);
		goto failed;
	}
	// This requires us to walk the chain of pci-pci bridges between the
	// root bridge and the device. Unimplemented.
	if (rootBus != data->pci.Bus)
	{
		log(route_irq, "Unimplemented! Device on bus %#x, but root is %#x\n",
				data->pci.Bus, rootBus);
		goto failed;
	}

	ResetBuffer(&buffer);
	status = AcpiGetIrqRoutingTable(Device, &buffer);
	CHECK_STATUS("AcpiGetIrqRoutingTable");
	//log(route_irq, "Got %u bytes of IRQ routing table\n", buffer.Length);
	ACPI_PCI_ROUTING_TABLE* route = buffer.Pointer;
	ACPI_PCI_ROUTING_TABLE* const end = buffer.Pointer + buffer.Length;
	//log(route_irq, "Routing table: %p..%p\n", route, end);
	UINT64 pciAddr = data->pci.Device;
	while (route < end && route->Length) {
		if ((route->Address >> 16) == pciAddr && route->Pin == data->pin) {
			found = route;
			break;
		}
		route = (ACPI_PCI_ROUTING_TABLE*)((char*)route + route->Length);
	}
	if (!found) {
		goto failed;
	}

	log(route_irq, "RouteIRQ: %02x:%02x.%d pin %d -> %s:%d\n",
		data->pci.Bus, data->pci.Device, data->pci.Function,
		found->Pin,
		found->Source[0] ? found->Source : NULL,
		found->SourceIndex);

	if (found->Source[0]) {
		status = RouteIRQLinkDevice(Device, found, data);
		//log(route_irq, "status %#x irq %#x\n", status, data->gsi);
		CHECK_STATUS("RouteIRQLinkDevice");
	} else {
		data->gsi = found->SourceIndex;
	}
	data->found = TRUE;
	status = AE_CTRL_TERMINATE;

failed:
	FreeBuffer(&buffer);
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

ACPI_STATUS RouteIRQ(ACPI_PCI_ID* device, int pin, int* irq) {
	IRQRouteData data = { *device, pin, 0, 0, 0, FALSE };
	ACPI_STATUS status = AE_OK;

	status = AcpiGetDevices("PNP0A03", RouteIRQCallback, &data, NULL);
	if (status == AE_OK)
	{
		if (data.found)
		{
			*irq = data.gsi
				| (data.triggering ? 0x100 : 0)
				| (data.polarity ? 0x200 : 0);
		}
		else
		{
			status = AE_NOT_FOUND;
		}
	}
	return_ACPI_STATUS(status);
}

