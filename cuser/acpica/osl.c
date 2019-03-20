#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <sb1.h>

#include "acpi.h"
#include "acpica.h"

static const bool log_osl = false;
static const bool log_global_lock = false;

UINT64 AcpiOsGetThreadId()
{
	// Note: 0 is an invalid thread ID according to AcpiUtCreateThreadState
	return 1;
}

__attribute__((noreturn)) static void unimpl(const char *tag)
{
    printf("UNIMPL: %s", tag);
    abort();
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
    return (AE_OK);
}


ACPI_STATUS AcpiOsTerminate ( void)
{
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
	ACPI_PHYSICAL_ADDRESS res = 0;
	if (AcpiFindRootPointer(&res) == AE_OK) {
		return res;
	} else {
		return 0;
	}
}

void * AcpiOsMapMemory (
    ACPI_PHYSICAL_ADDRESS   Where,
    ACPI_SIZE               Length)
{
	//log(map_memory, "mapping %x size %x => %x\n", Where, Length, ((char*)ACPI_PHYS_BASE) + Where);
	return ((char*)ACPI_PHYS_BASE) + Where;
}

void AcpiOsUnmapMemory (
    void                    *Where,
    ACPI_SIZE               Length)
{
	//log(map_memory, "Unmapping %p\n", Where, Length);
}

ACPI_STATUS
AcpiOsWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
	Width >>= 3;
//	log(io, "WritePort %#x value %#lx width %#x\n", Address, (UINT64)Value, Width);
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
//	log(io, "ReadPort %x width %x\n", Address, Width);
	*Value = portio(Address, Width, 0);
//	log(io, "ReadPort %x width %x ==> %x\n", Address, Width, *Value);
    return (AE_OK);
}

ACPI_STATUS
AcpiOsExecute (
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
    unimpl("AcpiOsExecute");
}

void AcpiOsWaitEventsComplete(void)
{
	// Wait for all callbacks queued with AcpiOsExecute to finish.
	unimpl("AcpiOsWaitEventsComplete");
}

void AcpiOsStall (
    UINT32                  microseconds)
{
    unimpl("AcpiOsStall");
}

void
AcpiOsSleep (
    UINT64                  milliseconds)
{
    unimpl("AcpiOsSleep");
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
    unimpl("AcpiOsWriteMemory");
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
    unimpl("AcpiOsReadMemory");
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

UINT32 PciReadWord(UINT32 Addr)
{
	AcpiOsWritePort(0xcf8, Addr, 32);
	UINT32 Temp = 0;
	AcpiOsReadPort(0xcfc, &Temp, 32);
	return Temp;
}

UINT32 AddrFromPciId(ACPI_PCI_ID* PciId, UINT32 Register)
{
	return 0x80000000 | (PciId->Bus << 16) | (PciId->Device << 11) | (PciId->Function << 8) | (Register & 0xfc);
}

ACPI_STATUS
AcpiOsReadPciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Register,
    UINT64                  *Value,
    UINT32                  Width)
{
	UINT32 Addr = AddrFromPciId(PciId, Register);
	AcpiOsWritePort(0xcf8, Addr, 32);
	UINT32 Temp;
	AcpiOsReadPort(0xcfc + (Register & 3), &Temp, Width);
	if (Width <= 32)
	{
		*Value = Temp;
	}
	else if (Width == 64)
	{
		*Value = (UINT64)Temp << 32 | PciReadWord(Addr + 4);
	}
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
	if (Width == 64) {
		AcpiOsWritePciConfiguration(PciId, Register + 4, Value >> 32, 32);
		Width = 32;
	}
	AcpiOsWritePort(0xcf8, AddrFromPciId(PciId, Register), 32);
	AcpiOsWritePort(0xcfc + (Register & 3), Value, Width);
    return (AE_OK);
}

ACPI_STATUS
AcpiOsSignal (
    UINT32                  Function,
    void                    *Info)
{
	const ACPI_SIGNAL_FATAL_INFO* FatalInfo = (const ACPI_SIGNAL_FATAL_INFO*)Info;

    switch (Function)
    {
    case ACPI_SIGNAL_FATAL:
		printf("ACPI fatal signal: %x %x %x\n",
				FatalInfo->Type,
				FatalInfo->Code,
				FatalInfo->Argument);

        break;

    case ACPI_SIGNAL_BREAKPOINT:
		printf("ACPI breakpoint signal: %s\n", Info);
        break;

    default:
		printf("ACPI other signal (%x)\n", Function);
        break;
    }

    return (AE_OK);
}

void AcpiOsFlushCache()
{
    log(osl, "FlushCache\n");
}

UINT64 AcpiOsGetTimer()
{
    unimpl("AcpiOsGetTimer");
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

// 1 = pending bit
// 2 = owned bit

uint32_t AcpiOsReleaseGlobalLock(ACPI_TABLE_FACS* facs)
{
	uint32_t* lock = (uint32_t*)(ACPI_PHYS_BASE + facs->GlobalLock);
	uint32_t new, old;
	do {
		old = *lock;
		new = old & ~3;
	} while (!__sync_bool_compare_and_swap(lock, old, new));
	log(global_lock, "Released global lock. Pending was %d\n", old & 1);
	return old & 1;
}

uint32_t AcpiOsAcquireGlobalLock(ACPI_TABLE_FACS* facs)
{
	uint32_t* lock = (uint32_t*)(ACPI_PHYS_BASE + facs->GlobalLock);
	uint32_t new, old;
	do
	{
		old = *lock;
		// if owned: we want to set it to owned and pending
		// if not owned: we want to set it to owned not pending
		// ignore previous pending-bit
		new = (old & ~1) | ((old & 2) >> 1) | 2;
	} while (!__sync_bool_compare_and_swap(lock, old, new));
	// Return true if acquired. We set the pending-bit if we started waiting.
	log(global_lock, "Acquired global lock: pending=%d\n", new&1);
	return !(new & 1);
}

// OS-specific processing to do "just before" setting the register values.
ACPI_STATUS AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegA, UINT32 RegB)
{
    return AE_OK;
}

// We don't use the debugger loop from dbinput.c, but just call
// AcpiDbCommandDispatch directly, so these should never get called really.
ACPI_STATUS AcpiOsWaitCommandReady()
{
    return AE_CTRL_TERMINATE;
}

ACPI_STATUS AcpiOsNotifyCommandComplete()
{
    return AE_CTRL_TERMINATE;
}
