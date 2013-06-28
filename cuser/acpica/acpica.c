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

    printf("_OSI returned %x\n", (UINT64) Object->Integer.Value);
    AcpiOsFree (Object);
    return Status;
}

#define CHECK_STATUS() do { if (ACPI_FAILURE(status)) { goto failed; } } while(0)

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

void start() {
	ACPI_STATUS status = AE_OK;

	printf("ACPICA: start\n");
    ACPI_FUNCTION_NAME (Examples-main);

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

    AcpiDbgLayer = ACPI_EXAMPLE;
    AcpiDbgLevel = ACPI_LV_INIT;
    ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "Example Debug output\n"));

    /* Example warning and error output */

    ACPI_INFO        ((AE_INFO, "ACPICA example info message"));
    ACPI_WARNING     ((AE_INFO, "ACPICA example warning message"));
    ACPI_ERROR       ((AE_INFO, "ACPICA example error message"));
    ACPI_EXCEPTION   ((AE_INFO, AE_AML_OPERAND_TYPE, "Example exception message"));

    status = ExecuteOSI ();
	CHECK_STATUS();
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
