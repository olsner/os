#include "common.h"
#include "acpica.h"

/******************************************************************************
 *
 * Example ACPICA handler and handler installation
 *
 *****************************************************************************/

static void NotifyHandler (
    ACPI_HANDLE                 Device,
    UINT32                      Value,
    void                        *Context)
{

    ACPI_INFO ((AE_INFO, "Received a notify 0x%x (device %p, context %p)", Value, Device, Context));
}

static ACPI_STATUS InstallHandlers (void)
{
    ACPI_STATUS             Status;


    /* Install global notify handler */

    Status = AcpiInstallNotifyHandler (ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY,
                                        NotifyHandler, NULL);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While installing Notify handler"));
        return (Status);
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * Example ACPICA initialization code. This shows a full initialization with
 * no early ACPI table access.
 *
 *****************************************************************************/

static ACPI_STATUS InitializeFullAcpi (void)
{
    ACPI_STATUS             Status;


    /* Initialize the ACPICA subsystem */

    Status = AcpiInitializeSubsystem ();
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While initializing ACPICA"));
        return (Status);
    }

    /* Initialize the ACPICA Table Manager and get all ACPI tables */

    Status = AcpiInitializeTables (NULL, 16, FALSE);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While initializing Table Manager"));
        return (Status);
    }

    /* Create the ACPI namespace from ACPI tables */

    Status = AcpiLoadTables ();
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While loading ACPI tables"));
        return (Status);
    }

    /* Install local handlers */

    Status = InstallHandlers ();
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While installing handlers"));
        return (Status);
    }

    /* Initialize the ACPI hardware */

    Status = AcpiEnableSubsystem (ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While enabling ACPICA"));
        return (Status);
    }

    /* Complete the ACPI namespace object initialization */

    Status = AcpiInitializeObjects (ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While initializing ACPICA objects"));
        return (Status);
    }

    return (AE_OK);
}


static ACPI_STATUS
ExecuteOSI (void)
{
    ACPI_STATUS             Status;
    ACPI_OBJECT_LIST        ArgList;
    ACPI_OBJECT             Arg[1];
    ACPI_BUFFER             ReturnValue;


    /* Setup input argument */

    ArgList.Count = 1;
    ArgList.Pointer = Arg;

    Arg[0].Type = ACPI_TYPE_INTEGER;
    Arg[0].Integer.Value = 0; // 1 = APIC mode. We're still in PIC dark ages :)

    ACPI_INFO ((AE_INFO, "Executing _PIC(%d)", Arg[0].Integer.Value));

    /* Ask ACPICA to allocate space for the return object */

    ReturnValue.Length = ACPI_ALLOCATE_BUFFER;

    Status = AcpiEvaluateObject (NULL, "\\_PIC", &ArgList, &ReturnValue);
	ACPI_FREE_BUFFER(ReturnValue);
	if (Status == AE_NOT_FOUND)
	{
		printf("\\_PIC was not found. Assuming that's ok.\n");
		return AE_OK;
	}
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While executing _PIC"));
        return Status;
    }

    printf("_PIC returned.\n");
    return Status;
}

#define CHECK_STATUS() do { if (ACPI_FAILURE(status)) { goto failed; } } while(0)

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


static ACPI_STATUS PrintAPICTable(void) {
	ACPI_TABLE_MADT* table = NULL;
	ACPI_STATUS status = AcpiGetTable("APIC", 0, (ACPI_TABLE_HEADER**)&table);
	CHECK_STATUS();

	printf("Found APIC table: %p\n", table);
	printf("Address of Local APIC: %#x\n", table->Address);
	printf("Flags: %#x\n", table->Flags);
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
			printf("%d: Local APIC. Processor ID %#x APIC ID %#x En=%d (%#x)\n", n,
				(int)apic->LocalApic.ProcessorId,
				(int)apic->LocalApic.Id,
				apic->LocalApic.LapicFlags & 1,
				apic->LocalApic.LapicFlags);
			break;
		case ACPI_MADT_TYPE_IO_APIC:
			printf("%d: I/O APIC. ID %#x Addr %#x GSI base %#x\n", n,
				(int)apic->IOApic.Id,
				apic->IOApic.Address,
				apic->IOApic.GlobalIrqBase);
			break;
		case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		{
			UINT32 flags = apic->InterruptOverride.IntiFlags;
			printf("%d: Interrupt Override. Source %#x GSI %#x Pol=%d Trigger=%d\n", n,
				apic->InterruptOverride.SourceIrq,
				apic->InterruptOverride.GlobalIrq,
				flags & 3, (flags >> 2) & 3);
			break;
		}
		case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		{
			UINT32 flags = apic->InterruptOverride.IntiFlags;
			printf("%d: Local APIC NMI. Processor ID %#x Pol=%d Trigger=%d LINT# %#x\n", n,
				apic->LocalApicNMI.ProcessorId,
				flags & 3, (flags >> 2) & 3,
				apic->LocalApicNMI.Lint);
			break;
		}
		default:
			printf("%d: Unknown APIC type %d\n", n, apic->Type);
			break;
		}
	}

	return status;
failed:
	printf("APIC table error %x\n", status);
	return status;
}

ACPI_STATUS PrintAcpiDevice(ACPI_HANDLE Device)
{
	printf("Found device %p\n", Device);
	ACPI_STATUS status = AE_OK;

	ACPI_DEVICE_INFO* info = NULL;
	status = AcpiGetObjectInfo(Device, &info);
	if (ACPI_SUCCESS(status)) {
		printf("Device flags %#x address %#x\n", info->Type, info->Flags, info->Address);
	}

	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

static ACPI_STATUS PrintDeviceCallback(ACPI_HANDLE Device, UINT32 Depth, void *Context, void** ReturnValue)
{
	return PrintAcpiDevice(Device);
}

// PNP0C0F = PCI Interrupt Link Device
// PNP0A03 = PCI Root Bridge
static ACPI_STATUS PrintDevices(void) {
	ACPI_STATUS status = AE_OK;

	printf("Searching for PNP0A03\n");
	status = AcpiGetDevices("PNP0A03", PrintDeviceCallback, NULL, NULL);
	CHECK_STATUS();

	printf("Searching for PNP0C0F\n");
	status = AcpiGetDevices("PNP0C0F", PrintDeviceCallback, NULL, NULL);
	CHECK_STATUS();

failed:
	return_ACPI_STATUS(status);
}

typedef struct IRQRouteData
{
	ACPI_PCI_ID pci;
	unsigned pin;
	// Need more data than this: is this on the PIC or an I/O APIC, link to the
	// relevant APIC information from the other table, etc.
	// For now: gsi is an IRQ number on the PIC
	int gsi;
	BOOLEAN found;
} IRQRouteData;

static void ResetBuffer(ACPI_BUFFER* buffer) {
	ACPI_FREE_BUFFER((*buffer));
	buffer->Pointer = 0;
	buffer->Length = ACPI_ALLOCATE_BUFFER;
}

static ACPI_STATUS RouteIRQLinkDevice(ACPI_HANDLE Device, ACPI_PCI_ROUTING_TABLE* found, IRQRouteData* data) {
	ACPI_STATUS status = AE_OK;
	ACPI_HANDLE LinkDevice = NULL;
	ACPI_BUFFER buffer = {0};

	printf("Routing IRQ Link device %s\n", found->Source);
	status = AcpiGetHandle(Device, found->Source, &LinkDevice);
	CHECK_STATUS();

	ResetBuffer(&buffer);
	status = AcpiGetCurrentResources(LinkDevice, &buffer);
	CHECK_STATUS();
	printf("Got %lu bytes of current resources\n", buffer.Length);
	ACPI_RESOURCE* resource = (ACPI_RESOURCE*)buffer.Pointer;
	printf("Got resource %p (status %#x), type %d\n", resource, status, resource->Type);
	switch (resource->Type) {
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		// There are more attributes here, e.g. triggering, polarity and
		// shareability. Since we're still in PIC mode, we already require
		// that all this is defaulty.
		printf("Extended IRQ: %d interrupts, first one %#x.\n",
			resource->Data.ExtendedIrq.InterruptCount,
			resource->Data.ExtendedIrq.Interrupts[0]);
		data->gsi = resource->Data.ExtendedIrq.Interrupts[0];
		break;
	case ACPI_RESOURCE_TYPE_IRQ:
		printf("IRQ: %d interrupts, first one %#x.\n",
			resource->Data.Irq.InterruptCount,
			resource->Data.Irq.Interrupts[0]);
		data->gsi = resource->Data.Irq.Interrupts[0];
		break;
	default:
		status = AE_BAD_DATA;
		goto failed;
	}
	status = AcpiSetCurrentResources(LinkDevice, &buffer);
	CHECK_STATUS();

failed:
	ACPI_FREE_BUFFER(buffer);
	return_ACPI_STATUS(status);
}

static ACPI_STATUS RouteIRQCallback(ACPI_HANDLE Device, UINT32 Depth, void *Context, void** ReturnValue)
{
	IRQRouteData* data = (IRQRouteData*)Context;
	ACPI_STATUS status = AE_OK;
	ACPI_RESOURCE* resource = NULL;
	ACPI_BUFFER buffer = {0};
	buffer.Length = ACPI_ALLOCATE_BUFFER;
	ACPI_PCI_ROUTING_TABLE* found = NULL;

	ACPI_DEVICE_INFO* info = NULL;
	status = AcpiGetObjectInfo(Device, &info);
	CHECK_STATUS();

	if (!(info->Flags & ACPI_PCI_ROOT_BRIDGE)) {
		goto failed;
	}

	printf("Root bridge with address %#x:\n", info->Address);
	int rootBus = -1;

	// Get _CRS, parse, check if the bus number range includes the one in
	// data->pci.Bus - then we've found the right *root* PCI bridge.
	// Though this might actually be a lot more complicated if we allow for
	// multiple root pci bridges.
	status = AcpiGetCurrentResources(Device, &buffer);
	CHECK_STATUS();
	//printf("Got %lu bytes of current resources\n", buffer.Length);
	//status = AcpiBufferToResource(buffer.Pointer, buffer.Length, &resource);
	resource = (ACPI_RESOURCE*)buffer.Pointer;
	//printf("Got resources %p (status %#x)\n", resource, status);
	CHECK_STATUS();
	while (resource->Type != ACPI_RESOURCE_TYPE_END_TAG) {
		printf("Got resource type %d\n", resource->Type);
		ACPI_RESOURCE_ADDRESS64 addr64;
		ACPI_STATUS status = AcpiResourceToAddress64(resource, &addr64);
		if (status == AE_OK && addr64.ResourceType == ACPI_BUS_NUMBER_RANGE)
		{
			printf("Root bridge bus range %#x..%#x\n",
					addr64.Minimum,
					addr64.Maximum);
			if (data->pci.Bus < addr64.Minimum ||
				data->pci.Bus > addr64.Maximum)
			{
				// This is not the root bridge we're looking for...
				goto failed;
			}
			rootBus = addr64.Minimum;
			break;
		}
		resource = ACPI_NEXT_RESOURCE(resource);
	}
	// dunno!
	if (rootBus == -1)
	{
		printf("Couldn't figure out the bus number for root bridge %#x\n",
				info->Address);
		goto failed;
	}
	// This requires us to walk the chain of pci-pci bridges between the
	// root bridge and the device. Unimplemented.
	if (rootBus != data->pci.Bus)
	{
		printf("Unimplemented! Device on bus %#x, but root is %#x\n",
				data->pci.Bus, rootBus);
		goto failed;
	}

	ResetBuffer(&buffer);
	status = AcpiGetIrqRoutingTable(Device, &buffer);
	CHECK_STATUS();
	printf("Got %u bytes of IRQ routing table\n", buffer.Length);
	ACPI_PCI_ROUTING_TABLE* route = buffer.Pointer;
	ACPI_PCI_ROUTING_TABLE* const end = buffer.Pointer + buffer.Length;
	printf("Routing table: %p..%p\n", route, end);
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

	printf("Found route: %#x pin %d -> %s:%d\n",
		found->Address >> 16,
		found->Pin,
		found->Source[0] ? found->Source : NULL,
		found->SourceIndex);

	if (found->Source[0]) {
		status = RouteIRQLinkDevice(Device, found, data);
		printf("status %#x irq %#x\n", status, data->gsi);
		CHECK_STATUS();
	} else {
		data->gsi = found->SourceIndex;
	}
	data->found = TRUE;
	status = AE_CTRL_TERMINATE;

failed:
	ACPI_FREE_BUFFER(buffer);
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

static ACPI_STATUS RouteIRQ(ACPI_PCI_ID* device, int pin, int* irq) {
	IRQRouteData data = { *device, pin, 0, FALSE };
	ACPI_STATUS status = AE_OK;

	status = AcpiGetDevices("PNP0A03", RouteIRQCallback, &data, NULL);
	if (status == AE_OK)
	{
		if (data.found)
		{
			*irq = data.gsi;
		}
		else
		{
			status = AE_NOT_FOUND;
		}
	}
	return_ACPI_STATUS(status);
}

// reserve some virtual memory space (never touched) to keep track pci device
// handles.
static const char pci_device_handles[65536] PLACEHOLDER_SECTION;

static void MsgFindPci(uintptr_t rcpt, uintptr_t arg)
{
	ACPI_PCI_ID temp = {0};
	u16 vendor = arg >> 16;
	u16 device = arg;
	uintptr_t addr = -1;
	printf("acpica: find pci %#x:%#x.\n", vendor, device);
	ACPI_STATUS status = FindPCIDevByVendor(vendor, device, &temp);
	if (ACPI_SUCCESS(status)) {
		addr = temp.Bus << 16 | temp.Device << 3 | temp.Function;
	}
	send1(MSG_ACPI_FIND_PCI, rcpt, addr);
}

static void MsgClaimPci(uintptr_t rcpt, uintptr_t addr, uintptr_t pins)
{
	addr &= 0xffff;
	ACPI_PCI_ID id = { 0, (addr >> 8) & 0xff, (addr >> 3) & 31, addr & 7 };
	printf("acpica: claim pci %02x:%02x.%x\n", id.Bus, id.Device, id.Function);

	// Set up whatever stuff to track PCI device drivers in general

	int irqs[4] = {0};
	for (int pin = 0; pin < 4; pin++) {
		if (!(pins & (1 << pin))) continue;

		ACPI_STATUS status = RouteIRQ(&id, 0, &irqs[pin]);
		CHECK_STATUS();
		printf("acpica: %02x:%02x.%x pin %d routed to IRQ %#x\n",
			id.Bus, id.Device, id.Function,
			pin, irqs[pin]);
	}

	pins = irqs[3] << 24 | irqs[2] << 16 | irqs[1] << 8 | irqs[0];

	send2(MSG_ACPI_CLAIM_PCI, rcpt, addr, pins);
	hmod(rcpt, (uintptr_t)pci_device_handles + addr, 0);
	return;

failed:
	send2(MSG_ACPI_CLAIM_PCI, rcpt, 0, 0);
}

void start() {
	ACPI_STATUS status = AE_OK;

	printf("ACPICA: start\n");

	// NB! Must be at least as large as physical memory - the ACPI tables could
	// be anywhere. (Could be handled by AcpiOsMapMemory though.)
	map(0, MAP_PHYS | PROT_READ, (void*)ACPI_PHYS_BASE, 0, USER_MAP_MAX - ACPI_PHYS_BASE);
	char* p = ((char*)ACPI_PHYS_BASE) + 0x100000;
	printf("Testing physical memory access: %p (0x100000): %x\n", p, *(u32*)p);

	__default_section_init();
	init_heap();

    ACPI_DEBUG_INITIALIZE (); /* For debug version only */
	status = InitializeFullAcpi ();
	CHECK_STATUS();

    /* Enable debug output, example debug print */

    AcpiDbgLayer = ACPI_EXAMPLE; //ACPI_ALL_COMPONENTS;
    AcpiDbgLevel = ACPI_LV_ALL_EXCEPTIONS | ACPI_LV_INTERRUPTS;

    status = ExecuteOSI ();
	CHECK_STATUS();
	// Tables we get in Bochs:
	// * DSDT: All the AML code
	// * FACS
	// * FACP
	// * APIC (= MADT)
	// * SSDT: Secondary System Description Table
	//   Contains more AML code loaded automatically by ACPICA
	// More tables on qemu:
	// * Another SSDT (Loaded by ACPICA)
	// * HPET table
//	PrintFACSTable();
//	PrintFACPTable();
	PrintAPICTable();
	// TODO Do something like PrintDevices to disable all pci interrupt link
	// devices (call _DIS). Then we'll enable them as we go along.
	PrintDevices();
	EnumeratePCI();
	ACPI_PCI_ID temp;
	status = FindPCIDevByVendor(0x8086, 0x100e, &temp);
	CHECK_STATUS();
	int irq;
	status = RouteIRQ(&temp, 0, &irq);
	CHECK_STATUS();
	printf("e1000 pin 0 got routed to %#x\n", irq);
	printf("OSI executed successfullly, now initializing debugger.\n");
	//status = AcpiDbUserCommands (ACPI_DEBUGGER_COMMAND_PROMPT, NULL);
	//CHECK_STATUS();

	AcpiWriteBitRegister(ACPI_BITREG_SCI_ENABLE, 1);
	AcpiWriteBitRegister(ACPI_BITREG_POWER_BUTTON_ENABLE, 1);

	printf("Waiting for SCI interrupts...\n");
	for (;;) {
		// Do some kind of trick with AcpiOsGetLine and the debugger to let us
		// loop around here, processing interrupts and what-not, then calling
		// into the debugger when we have received a full line.
		uintptr_t rcpt = 0x100;
		uintptr_t arg = 0;
		uintptr_t arg2 = 0;
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		printf("acpica: Received %#lx from %#lx: %#lx %#lx\n", msg, rcpt, arg, arg2);
		if (msg == MSG_PULSE) {
			if (AcpiOsCheckInterrupt(rcpt, arg)) {
				printf("acpica: Handled interrupt.\n", msg, rcpt, arg);
				// It was an IRQ, handled by ACPICA
				continue;
			} else {
				printf("acpica: Unhandled pulse: %#x from %#lx\n", arg, rcpt);
			}
		}
		switch (msg & 0xff)
		{
		case MSG_ACPI_FIND_PCI:
			MsgFindPci(rcpt, arg);
			break;
		case MSG_ACPI_CLAIM_PCI:
			MsgClaimPci(rcpt, arg, arg2);
			break;
		// This feels a bit wrong, but as long as we use PIO access to PCI
		// configuration space, we need to serialize all accesses.
		case MSG_ACPI_READ_PCI:
			arg = PciReadWord((arg & 0x7ffffffc) | 0x80000000);
			send1(MSG_ACPI_READ_PCI, rcpt, arg);
			break;
		}
		// TODO Handle other stuff.
		if (rcpt == 0x100)
		{
			hmod(rcpt, 0, 0);
		}
	}
	status = AcpiTerminate();
	CHECK_STATUS();
	printf("Acpi terminated... Halting.\n");

failed:
	printf("ACPI failed :( (status %x)\n", status);
	for (;;);
}
