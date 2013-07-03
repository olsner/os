#include "common.h"

static const uintptr_t acpi_handle = 4;

void start() {
	uintptr_t dummy = 0;
	// Ask ACPICA/PCI to find one e1000 device and then claim it
	recv1(&dummy, &dummy);
	for(;;);
}
