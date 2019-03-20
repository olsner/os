#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sb1.h>

#include "common.h"
#include "acpica.h"

static const bool log_claim_pci = false;

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

    ACPI_INFO(("Received a notify 0x%x (device %p, context %p)", Value, Device, Context));
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

    Status = AcpiInitializeTables (NULL, 0, FALSE);
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


static ACPI_STATUS ExecuteOSI(int pic_mode)
{
    ACPI_STATUS             Status;
    ACPI_OBJECT_LIST        ArgList;
    ACPI_OBJECT             Arg[1];
    ACPI_BUFFER             ReturnValue;


    /* Setup input argument */

    ArgList.Count = 1;
    ArgList.Pointer = Arg;

    Arg[0].Type = ACPI_TYPE_INTEGER;
    Arg[0].Integer.Value = pic_mode;

    /* Ask ACPICA to allocate space for the return object */

    ReturnValue.Pointer = 0;
    ReturnValue.Length = ACPI_ALLOCATE_BUFFER;

    Status = AcpiEvaluateObject (NULL, "\\_PIC", &ArgList, &ReturnValue);
	FreeBuffer(&ReturnValue);
	if (Status == AE_NOT_FOUND)
	{
		return AE_OK;
	}
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While executing _PIC"));
        return Status;
    }

    return AE_OK;
}

// reserve some virtual memory space (never touched) to keep track pci device
// handles.
static const char pci_device_handles[65536] PLACEHOLDER_SECTION;

static void MsgFindPci(uintptr_t rcpt, uintptr_t arg)
{
	ACPI_PCI_ID temp = { 0, 0, 0, 0 };
	u16 vendor = arg >> 16;
	u16 device = arg;
	uintptr_t addr = -1;
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
	log(claim_pci, "claim pci %02x:%02x.%x\n", id.Bus, id.Device, id.Function);

	// Set up whatever stuff to track PCI device drivers in general

	int irqs[4] = {0};
	for (int pin = 0; pin < 4; pin++) {
		if (!(pins & (1 << pin))) continue;

		ACPI_STATUS status = RouteIRQ(&id, 0, &irqs[pin]);
		CHECK_STATUS("RouteIRQ");
		log(claim_pci, "%02x:%02x.%x pin %d routed to IRQ %#x\n",
			id.Bus, id.Device, id.Function,
			pin, irqs[pin]);
	}

	if (pins & ACPI_PCI_CLAIM_MASTER) {
		u64 value;
		AcpiOsReadPciConfiguration(&id, PCI_COMMAND, &value, 16);
		if (!(value & PCI_COMMAND_MASTER)) {
			value |= PCI_COMMAND_MASTER;
			AcpiOsWritePciConfiguration(&id, PCI_COMMAND, value, 16);
		}
	}

	pins = (u64)irqs[3] << 48 | (u64)irqs[2] << 32 | irqs[1] << 16 | irqs[0];

	send2(MSG_ACPI_CLAIM_PCI, rcpt, addr, pins);
	hmod(rcpt, (uintptr_t)pci_device_handles + addr, 0);
	return;

failed:
	send2(MSG_ACPI_CLAIM_PCI, rcpt, 0, 0);
}

static size_t debugger_buffer_pos = 0;

static void debugger_pre_cmd(void) {
	debugger_buffer_pos = 0;
	AcpiGbl_MethodExecuting = FALSE;
	AcpiGbl_StepToNextCall = FALSE;
	AcpiDbSetOutputDestination(ACPI_DB_CONSOLE_OUTPUT);
}

static void GlobalEventHandler(UINT32 EventType, ACPI_HANDLE Device,
		UINT32 EventNumber, void *Context) {
	if (EventType == ACPI_EVENT_TYPE_FIXED &&
		EventNumber == ACPI_EVENT_POWER_BUTTON) {

		printf("POWER BUTTON! Shutting down.\n");

		AcpiEnterSleepStatePrep(ACPI_STATE_S5);
		AcpiEnterSleepState(ACPI_STATE_S5);
	}
}

void start() {
	ACPI_STATUS status = AE_OK;

	printf("acpica: starting...\n");

	// NB! Must be at least as large as physical memory - the ACPI tables could
	// be anywhere. (Could be handled by AcpiOsMapMemory though.)
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
		(void*)ACPI_PHYS_BASE, 0, USER_MAP_MAX - ACPI_PHYS_BASE);

	__default_section_init();

    AcpiDbgLayer = 0;
    AcpiDbgLevel = ACPI_LV_REPAIR | ACPI_LV_INTERRUPTS;

	status = InitializeFullAcpi ();
	CHECK_STATUS("InitializeFullAcpi");

	int pic_mode = 0; // Default is PIC mode if something fails
	status = PrintAPICTable();
	CHECK_STATUS("PrintAPICTable");
	status = FindIOAPICs(&pic_mode);
	CHECK_STATUS("Find IOAPIC");
	status = ExecuteOSI(pic_mode);
	CHECK_STATUS("ExecuteOSI");
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
	// TODO Iterate through and disable all pci interrupt link devices (call
	// _DIS). Then we'll enable the ones we actually intend to use.

	EnumeratePCI();

	AcpiWriteBitRegister(ACPI_BITREG_SCI_ENABLE, 1);
	//AcpiWriteBitRegister(ACPI_BITREG_POWER_BUTTON_ENABLE, 1);
	AcpiInstallGlobalEventHandler(GlobalEventHandler, NULL);
	AcpiEnableEvent(ACPI_EVENT_POWER_BUTTON, 0);

	for (;;) {
		ipc_dest_t rcpt = 0x100;
		ipc_arg_t arg = 0;
		ipc_arg_t arg2 = 0;
		ipc_msg_t msg = recv2(&rcpt, &arg, &arg2);
		//printf("acpica: Received %#lx from %#lx: %#lx %#lx\n", msg, rcpt, arg, arg2);
		if (msg == MSG_PULSE) {
			if (AcpiOsCheckInterrupt(rcpt, arg)) {
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
		case MSG_ACPI_DEBUGGER_INIT:
			debugger_pre_cmd();
			send0(MSG_ACPI_DEBUGGER_INIT, rcpt);
			break;
		case MSG_ACPI_DEBUGGER_BUFFER:
			assert(debugger_buffer_pos < ACPI_DB_LINE_BUFFER_SIZE);
			AcpiGbl_DbLineBuf[debugger_buffer_pos++] = arg;
			send0(MSG_ACPI_DEBUGGER_BUFFER, rcpt);
			break;
		case MSG_ACPI_DEBUGGER_CMD:
			assert(debugger_buffer_pos < ACPI_DB_LINE_BUFFER_SIZE);
			AcpiGbl_DbLineBuf[debugger_buffer_pos++] = 0;
			putchar('\n');
			AcpiDbCommandDispatch(AcpiGbl_DbLineBuf, NULL, NULL);
			debugger_pre_cmd();
			send0(MSG_ACPI_DEBUGGER_CMD, rcpt);
			break;
		case MSG_ACPI_DEBUGGER_CLR_BUFFER:
			debugger_pre_cmd();
			send0(MSG_ACPI_DEBUGGER_CLR_BUFFER, rcpt);
			break;
		case MSG_REG_IRQ:
			RegIRQ(rcpt, arg);
			continue;
		case MSG_IRQ_ACK:
			AckIRQ(rcpt);
			continue;
		}
		// TODO Handle other stuff.
		if (rcpt == 0x100)
		{
			hmod(rcpt, 0, 0);
		}
	}
	__builtin_unreachable();

failed:
	printf("ACPI failed :( (status %x)\n", status);
	abort();
}
