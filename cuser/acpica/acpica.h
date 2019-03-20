#include "acpi.h"
#include "accommon.h"
#include "acdebug.h"

#define log(scope, fmt, ...) \
	do { if (log_## scope) { printf("acpica: " fmt, ## __VA_ARGS__); } } while (0)

#define CHECK_STATUS(fmt, ...) do { if (ACPI_FAILURE(status)) { \
	printf("ACPI failed (%d): " fmt "\n", status, ## __VA_ARGS__); \
	goto failed; \
	} } while(0)

#define _COMPONENT ACPI_DRIVER
ACPI_MODULE_NAME("acpica")

int AcpiOsCheckInterrupt(uintptr_t rcpt, uintptr_t arg);
void RegIRQ(uintptr_t rcpt, uintptr_t int_spec);
void AckIRQ(uintptr_t rcpt);

ACPI_STATUS PrintAPICTable(void);
ACPI_STATUS FindIOAPICs(int *pic_mode);
void AddPIC(void);
ACPI_STATUS RouteIRQ(ACPI_PCI_ID* device, int pin, int* irq);

ACPI_STATUS EnumeratePCI(void);
ACPI_STATUS PrintAcpiDevice(ACPI_HANDLE Device);

UINT32 PciReadWord(UINT32 Addr);
UINT32 AddrFromPciId(ACPI_PCI_ID* PciId, UINT32 Register);

ACPI_STATUS FindPCIDevByVendor(UINT16 vendor, UINT16 device, ACPI_PCI_ID* id);

static void FreeBuffer(ACPI_BUFFER* buffer) {
	AcpiOsFree(buffer->Pointer);
}
static void ResetBuffer(ACPI_BUFFER* buffer) {
	FreeBuffer(buffer);
	buffer->Pointer = 0;
	buffer->Length = ACPI_ALLOCATE_BUFFER;
}

#include "msg_acpi.h"
