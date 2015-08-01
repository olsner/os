#include "acpi.h"
#include "accommon.h"
#include "acdebug.h"

#define _COMPONENT          ACPI_EXAMPLE
        ACPI_MODULE_NAME    ("acpica")

int AcpiOsCheckInterrupt(uintptr_t rcpt, uintptr_t arg);
ACPI_STATUS EnumeratePCI(void);
ACPI_STATUS PrintAcpiDevice(ACPI_HANDLE Device);

UINT32 PciReadWord(UINT32 Addr);
UINT32 AddrFromPciId(ACPI_PCI_ID* PciId, UINT32 Register);

ACPI_STATUS FindPCIDevByVendor(u16 vendor, u16 device, ACPI_PCI_ID* id);
ACPI_STATUS FindPCIDevByClass(u32 classcode, ACPI_PCI_ID* id);

void init_heap(void);

