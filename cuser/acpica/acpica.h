#include "acpi.h"

int AcpiOsCheckInterrupt(uintptr_t rcpt, uintptr_t arg);
void EnumeratePCI(void);
ACPI_STATUS PrintAcpiDevice(ACPI_HANDLE Device);

void init_heap(void);

