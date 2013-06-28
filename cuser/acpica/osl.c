#include <stdarg.h>
#include "common.h"
#include "acpi.h"

extern void* malloc(size_t size);
extern void free(void* p);

UINT64 AcpiOsGetThreadId()
{
	// Note: 0 is an invalid thread ID according to AcpiUtCreateThreadState
	return 1;
}

#ifdef ACPI_SINGLE_THREADED
/******************************************************************************
 *
 * FUNCTION:    Semaphore stub functions
 *
 * DESCRIPTION: Stub functions used for single-thread applications that do
 *              not require semaphore synchronization. Full implementations
 *              of these functions appear after the stubs.
 *
 *****************************************************************************/

ACPI_STATUS
AcpiOsCreateSemaphore (
    UINT32              MaxUnits,
    UINT32              InitialUnits,
    ACPI_HANDLE         *OutHandle)
{
    *OutHandle = (ACPI_HANDLE) 1;
    return (AE_OK);
}

ACPI_STATUS
AcpiOsDeleteSemaphore (
    ACPI_HANDLE         Handle)
{
    return (AE_OK);
}

ACPI_STATUS
AcpiOsWaitSemaphore (
    ACPI_HANDLE         Handle,
    UINT32              Units,
    UINT16              Timeout)
{
    return (AE_OK);
}

ACPI_STATUS
AcpiOsSignalSemaphore (
    ACPI_HANDLE         Handle,
    UINT32              Units)
{
    return (AE_OK);
}

ACPI_STATUS AcpiOsCreateLock (ACPI_SPINLOCK           *OutHandle)
{
	*OutHandle = (ACPI_SPINLOCK)1;
    return AE_OK;
}


void
AcpiOsDeleteLock (
    ACPI_SPINLOCK           Handle)
{
}


ACPI_CPU_FLAGS
AcpiOsAcquireLock (
    ACPI_HANDLE             Handle)
{
    return (0);
}


void
AcpiOsReleaseLock (
    ACPI_SPINLOCK           Handle,
    ACPI_CPU_FLAGS          Flags)
{
}

#else
#error only single-threaded ACPI supported
#endif

ACPI_STATUS AcpiOsInitialize ( void)
{
	printf("AcpiOsInitialize\n");
    return (AE_OK);
}


ACPI_STATUS AcpiOsTerminate ( void)
{
	printf("AcpiOsInitialize\n");
    return (AE_OK);
}

void* AcpiOsAllocate(ACPI_SIZE size)
{
    return malloc((size_t) size);
}

void AcpiOsFree(void *mem)
{
    free(mem);
}

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer()
{
	ACPI_SIZE res = 0;
	if (AcpiFindRootPointer(&res) == AE_OK) {
		return (ACPI_PHYSICAL_ADDRESS)res;
	} else {
		return 0;
	}
}

void * AcpiOsMapMemory (
    ACPI_PHYSICAL_ADDRESS   Where,
    ACPI_SIZE               Length)
{
	printf("mapping %x size %x => %x\n", Where, Length, ((char*)ACPI_PHYS_BASE) + Where);
	return ((char*)ACPI_PHYS_BASE) + Where;
}

void AcpiOsUnmapMemory (
    void                    *Where,
    ACPI_SIZE               Length)
{
	printf("Unmapping %p\n", Where, Length);
}

ACPI_STATUS
AcpiOsWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
	Width >>= 3;
	printf("WritePort %x value %x width %x\n", Address, Value, Width);
	portio(Address, Width | 0x10, Value);
    return (AE_OK);
}

ACPI_STATUS
AcpiOsReadPort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
	// Width is in bits! Our APIs expect bytes.
	Width >>= 3;
	printf("ReadPort %x width %x\n", Address, Width);
	*Value = portio(Address, Width, 0);
	printf("ReadPort %x width %x ==> %x\n", Address, Width, *Value);
    return (AE_OK);
}

ACPI_STATUS
AcpiOsExecute (
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
	printf("AcpiOsExecute(%x)\n", Type);
	return AE_NOT_IMPLEMENTED;
}

void AcpiOsWaitEventsComplete(void)
{
	printf("AcpiOsWaitEventsComplete\n");
	// Wait for all callbacks queued with AcpiOsExecute to finish.
}

void AcpiOsStall (
    UINT32                  microseconds)
{
	printf("AcpiOsStall: %xus\n", microseconds);
}

void
AcpiOsSleep (
    UINT64                  milliseconds)
{
	printf("AcpiOsSleep: %xms\n", milliseconds);
}

static const u64 con_handle = 3;

char getchar() {
	uintptr_t c = 0;
	uintptr_t handle = con_handle;
	ipc1(MSG_CON_READ, &handle, &c);
	return c;
}

ACPI_STATUS
AcpiOsGetLine (
    char                    *Buffer,
    UINT32                  BufferLength,
    UINT32                  *BytesRead)
{
    int                     Temp;
    UINT32                  i;

    for (i = 0; ; i++)
    {
        if (i >= BufferLength)
        {
            return (AE_BUFFER_OVERFLOW);
        }

        Temp = getchar ();
        if (!Temp || Temp == '\n')
        {
            break;
        }

        Buffer [i] = (char) Temp;
    }

    /* Null terminate the buffer */

    Buffer [i] = 0;

    /* Return the number of bytes in the string */

    if (BytesRead)
    {
        *BytesRead = i;
    }
    return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsTableOverride
 *
 * PARAMETERS:  ExistingTable       - Header of current table (probably
 *                                    firmware)
 *              NewTable            - Where an entire new table is returned.
 *
 * RETURN:      Status, pointer to new table. Null pointer returned if no
 *              table is available to override
 *
 * DESCRIPTION: Return a different version of a table if one is available
 *
 *****************************************************************************/

ACPI_STATUS
AcpiOsTableOverride (
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_TABLE_HEADER       **NewTable)
{

    if (!ExistingTable || !NewTable)
    {
        return (AE_BAD_PARAMETER);
    }

    *NewTable = NULL;
    return (AE_NO_ACPI_TABLES);
}

ACPI_STATUS
AcpiOsPhysicalTableOverride (
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_PHYSICAL_ADDRESS   *NewAddress,
    UINT32                  *NewTableLength)
{

    return (AE_SUPPORT);
}

ACPI_STATUS
AcpiOsWriteMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  Value,
    UINT32                  Width)
{
	printf("AcpiOsWriteMemory %p := %x (width %x)\n", Address, Value, Width);
    return (AE_OK);
}

BOOLEAN
AcpiOsReadable (
    void                    *Pointer,
    ACPI_SIZE               Length)
{
    return (TRUE);
}

BOOLEAN
AcpiOsWritable (
    void                    *Pointer,
    ACPI_SIZE               Length)
{
    return (TRUE);
}

ACPI_STATUS
AcpiOsReadMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  *Value,
    UINT32                  Width)
{

    switch (Width)
    {
    case 8:
    case 16:
    case 32:
    case 64:

        *Value = 0;
        break;

    default:

        return (AE_BAD_PARAMETER);
    }
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiOsReadPciConfiguration
 *
 * PARAMETERS:  PciId               - Seg/Bus/Dev
 *              Register            - Device Register
 *              Value               - Buffer where value is placed
 *              Width               - Number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read data from PCI configuration space
 *
 *****************************************************************************/

ACPI_STATUS
AcpiOsReadPciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Register,
    UINT64                  *Value,
    UINT32                  Width)
{
	printf("AcpiOsReadPciConfiguration %x %x\n", *(u64*)PciId, Register);
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiOsWritePciConfiguration
 *
 * PARAMETERS:  PciId               - Seg/Bus/Dev
 *              Register            - Device Register
 *              Value               - Value to be written
 *              Width               - Number of bits
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Write data to PCI configuration space
 *
 *****************************************************************************/

ACPI_STATUS
AcpiOsWritePciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Register,
    UINT64                  Value,
    UINT32                  Width)
{
	printf("AcpiOsWritePciConfiguration %x %x\n", *(u64*)PciId, Register);
    return (AE_OK);
}

UINT32
AcpiOsInstallInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
	printf("AcpiOsInstallInterruptHandler %x\n", InterruptNumber);
    return (AE_OK);
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
    return (AE_OK);
}

ACPI_STATUS
AcpiOsSignal (
    UINT32                  Function,
    void                    *Info)
{
	ACPI_SIGNAL_FATAL_INFO* FatalInfo = (ACPI_SIGNAL_FATAL_INFO*)Info;

    switch (Function)
    {
    case ACPI_SIGNAL_FATAL:
		printf("ACPI breakpoint: %x %x %x\n",
				FatalInfo->Type,
				FatalInfo->Code,
				FatalInfo->Argument);

        break;

    case ACPI_SIGNAL_BREAKPOINT:
		printf("ACPI breakpoint: %s\n", Info);
        break;

    default:

        break;
    }

    return (AE_OK);
}

UINT64 AcpiOsGetTimer()
{
	printf("GetTimer\n");
	return 0;
}

ACPI_STATUS
AcpiOsPredefinedOverride (
    const ACPI_PREDEFINED_NAMES *InitVal,
    ACPI_STRING                 *NewVal)
{

    if (!InitVal || !NewVal)
    {
        return (AE_BAD_PARAMETER);
    }

    *NewVal = NULL;
    return (AE_OK);
}

