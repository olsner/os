#include "acpi.h"
#include "accommon.h"
#include "acdebug.h"

#define _COMPONENT          ACPI_EXAMPLE
        ACPI_MODULE_NAME    ("acpica")

int AcpiOsCheckInterrupt(uintptr_t rcpt, uintptr_t arg);
ACPI_STATUS EnumeratePCI(void);
ACPI_STATUS PrintAcpiDevice(ACPI_HANDLE Device);

void init_heap(void);

