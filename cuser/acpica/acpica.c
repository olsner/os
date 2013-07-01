#include "common.h"
#include "acpi.h"
#include "accommon.h"
#include "acdebug.h"

extern void init_heap(void);

#define _COMPONENT          ACPI_EXAMPLE
        ACPI_MODULE_NAME    ("acpica")

/******************************************************************************
 *
 * Example ACPICA handler and handler installation
 *
 *****************************************************************************/

void NotifyHandler (
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


/******************************************************************************
 *
 * Example control method execution.
 *
 * _OSI is a predefined method that is implemented internally within ACPICA.
 *
 * Shows the following elements:
 *
 * 1) How to setup a control method argument and argument list
 * 2) How to setup the return value object
 * 3) How to invoke AcpiEvaluateObject
 * 4) How to check the returned ACPI_STATUS
 * 5) How to analyze the return value
 *
 *****************************************************************************/

static ACPI_STATUS
ExecuteOSI (void)
{
    ACPI_STATUS             Status;
    ACPI_OBJECT_LIST        ArgList;
    ACPI_OBJECT             Arg[1];
    ACPI_BUFFER             ReturnValue;
    ACPI_OBJECT             *Object;


    ACPI_INFO ((AE_INFO, "Executing OSI method"));

    /* Setup input argument */

    ArgList.Count = 1;
    ArgList.Pointer = Arg;

    Arg[0].Type = ACPI_TYPE_STRING;
    Arg[0].String.Pointer = "Windows 2001";
    Arg[0].String.Length = sizeof("Windows 2001") - 1;

    /* Ask ACPICA to allocate space for the return object */

    ReturnValue.Length = ACPI_ALLOCATE_BUFFER;

    Status = AcpiEvaluateObject (NULL, "\\_OSI", &ArgList, &ReturnValue);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While executing _OSI"));
        return Status;
    }

    /* Ensure that the return object is large enough */

    if (ReturnValue.Length < sizeof (ACPI_OBJECT))
    {
        AcpiOsPrintf ("Return value from _OSI method too small, %.8X\n",
            ReturnValue.Length);
        return AE_ERROR;
    }

    /* Expect an integer return value from execution of _OSI */

    Object = ReturnValue.Pointer;
    if (Object->Type != ACPI_TYPE_INTEGER)
    {
        AcpiOsPrintf ("Invalid return type from _OSI, %.2X\n", Object->Type);
		Status = AE_ERROR;
    }

    printf("_OSI returned %#lx\n", Object->Integer.Value);
    AcpiOsFree (Object);
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


static ACPI_STATUS PrintAPICTable() {
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

ACPI_STATUS PrintDeviceCallback(ACPI_HANDLE Device, UINT32 Depth, void *Context, void** ReturnValue)
{
	printf("Found device %p\n", Device);
	ACPI_STATUS status = AE_OK;
	ACPI_BUFFER buffer = {0};
	buffer.Length = ACPI_ALLOCATE_BUFFER;

	ACPI_DEVICE_INFO* info = NULL;
	status = AcpiGetObjectInfo(Device, &info);
	CHECK_STATUS();
	printf("Device flags %#x address %#x\n", info->Type, info->Flags, info->Address);

	if (info->Flags & ACPI_PCI_ROOT_BRIDGE) {
		status = AcpiGetIrqRoutingTable(Device, &buffer);
		CHECK_STATUS();
		printf("Got %u bytes of IRQ routing table\n", buffer.Length);
	}

failed:
	ACPI_FREE_BUFFER(buffer);
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

// PNP0C0F = PCI Interrupt Link Device
// PNP0A03 = PCI Root Bridge
ACPI_STATUS PrintDevices() {
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

// FIXME Workaround for the fact that anonymous mappings can only span a single
// page (currently).
static void mapAnonPages(enum prot prot, void *local_addr, uintptr_t size) {
	uintptr_t i = 0;
	while (i < size) {
		syscall5(MSG_MAP,
			0, MAP_ANON | prot, (uintptr_t)local_addr + i, 0, 0x1000);
		i += 0x1000;
	}
}

void EnumeratePCI();

void start() {
	ACPI_STATUS status = AE_OK;

	printf("ACPICA: start\n");

	// NB! Must be at least as large as physical memory - the ACPI tables could
	// be anywhere. (Could be handled by AcpiOsMapMemory though.)
	map(0, PROT_READ | PROT_WRITE, (void*)ACPI_PHYS_BASE, 0, 32 * 1024 * 1024);
	char* p = ((char*)ACPI_PHYS_BASE) + 0x100000;
	printf("%p (0x100000): %x\n", p, *(u64*)p);

	mapAnonPages(PROT_READ | PROT_WRITE, __bss_start, __bss_end - __bss_start);
	printf("mapped bss %x..%x\n", __bss_start, __bss_end);
	// Copy __data_size bytes from __data_lma to __data_vma.
	printf("Copying initialized data...\n");
	AcpiUtMemcpy(__data_vma, __data_lma, (uint64_t)&__data_size);

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
	PrintDevices();
	EnumeratePCI();
	printf("OSI executed successfullly, now initializing debugger.\n");
	for (;;) {
        status = AcpiDbUserCommands (ACPI_DEBUGGER_COMMAND_PROMPT, NULL);
		CHECK_STATUS();
	}
	status = AcpiTerminate();
	CHECK_STATUS();
	printf("Acpi terminated... Halting.\n");

failed:
	printf("ACPI failed :( (status %x)\n", status);
	for (;;);
}
