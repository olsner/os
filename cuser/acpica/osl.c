#include <stdarg.h>
#include "common.h"
#include "acpi.h"
#include "acpica.h"

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
	//printf("mapping %x size %x => %x\n", Where, Length, ((char*)ACPI_PHYS_BASE) + Where);
	return ((char*)ACPI_PHYS_BASE) + Where;
}

void AcpiOsUnmapMemory (
    void                    *Where,
    ACPI_SIZE               Length)
{
	//printf("Unmapping %p\n", Where, Length);
}

ACPI_STATUS
AcpiOsWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
	Width >>= 3;
//	printf("WritePort %#x value %#lx width %#x\n", Address, (UINT64)Value, Width);
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
//	printf("ReadPort %x width %x\n", Address, Width);
	*Value = portio(Address, Width, 0);
//	printf("ReadPort %x width %x ==> %x\n", Address, Width, *Value);
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
	printf("AcpiOsStall: %lu us\n", microseconds);
}

void
AcpiOsSleep (
    UINT64                  milliseconds)
{
	printf("AcpiOsSleep: %lu ms\n", milliseconds);
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
		putchar(Temp);
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
	printf("AcpiOsReadMemory %p width %d\n", Address, Width);
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
	//printf("AcpiOsReadPciConfiguration %02x:%02x.%x reg %x w %d\n", PciId->Bus, PciId->Device, PciId->Function, Register, Width);
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
	//printf("AcpiOsReadPciConfiguration %02x:%02x.%x reg %x w %d := %lx\n", PciId->Bus, PciId->Device, PciId->Function, Register, Width, Value);
	if (Width == 64) {
		AcpiOsWritePciConfiguration(PciId, Register + 4, Value >> 32, 32);
		Width = 32;
	}
	AcpiOsWritePort(0xcf8, AddrFromPciId(PciId, Register), 32);
	AcpiOsWritePort(0xcfc + (Register & 3), Value, Width);
    return (AE_OK);
}

static const uintptr_t pic_handle = 2;

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

	hmod(pic_handle, pic_handle, (uintptr_t)reg);
	uintptr_t irq = InterruptNumber;
	uintptr_t msg = sendrcv1(MSG_REG_IRQ, (uintptr_t)reg, &irq);
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

static void HandleIrq(irq_reg* irq, uintptr_t num) {
	printf("IRQ %#lx: Calling %p/%p (registered for %#x)\n", num,
			irq->ServiceRoutine, irq->Context, irq->InterruptNumber);
	irq->ServiceRoutine(irq->Context);
}

int AcpiOsCheckInterrupt(uintptr_t rcpt, uintptr_t arg)
{
	irq_reg* irq = irq_regs;
	while (irq) {
		if ((uintptr_t)irq == rcpt) {
			HandleIrq(irq, arg);
			send1(MSG_IRQ_ACK, rcpt, arg);
			return 1;
		}
		irq = irq->Next;
	}
	return 0;
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

// 1 = pending bit
// 2 = owned bit

uint32_t AcpiOsReleaseGlobalLock(ACPI_TABLE_FACS* facs)
{
	uint32_t* lock = ACPI_CAST32(ACPI_PHYS_BASE + facs->GlobalLock);
	uint32_t new, old;
	do {
		old = *lock;
		new = old & ~3;
	} while (!__sync_bool_compare_and_swap(lock, old, new));
	printf("Released global lock. Pending was %d\n", old & 1);
	return old & 1;
}

uint32_t AcpiOsAcquireGlobalLock(ACPI_TABLE_FACS* facs)
{
	uint32_t* lock = ACPI_CAST32(ACPI_PHYS_BASE + facs->GlobalLock);
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
	printf("Acquired global lock: pending=%d\n", new&1);
	return !(new & 1);
}

