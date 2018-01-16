#include <stdio.h>
#include <string.h>

#include "common.h"
#include "acpi.h"
#include "acpica.h"

static UINT32 getPCIConfig(u8 bus, u8 dev, u8 func, u8 offset, UINT32 width)
{
	UINT64 ret;
	ACPI_PCI_ID id = { 0, bus, dev, func };
	AcpiOsReadPciConfiguration(&id, offset, &ret, width);
	return ret;
}

typedef struct PCIEnum
{
	// Return AE_CTRL_TERMINATE to stop iterating
	ACPI_STATUS (*cb)(struct PCIEnum* context);
	union {
		struct { u16 vendor, device; };
	} in;
	struct {
		ACPI_PCI_ID pci_id;
		u16 vendor;
		u16 device;
	} cur;
} PCIEnum;

#define getVendorID(b,d,f) getPCIConfig(b,d,f, 0, 16)
#define getDeviceID(b,d,f) getPCIConfig(b,d,f, 2, 16)
#define getHeaderType(b,d,f) getPCIConfig(b,d,f, 14, 8)
#define getSubClass(b,d,f) getPCIConfig(b,d,f, 10, 8)
#define getBaseClass(b,d,f) getPCIConfig(b,d,f, 11, 8)
#define getSecondaryBus(b,d,f) getPCIConfig(b,d,f, 0x19, 8)

#define ACPI_RETURN_IF(e) \
	do { \
		ACPI_STATUS status__ = (e); \
		/* Disable error logging for control codes. */ \
		if ((status__ & AE_CODE_MASK) == AE_CODE_CONTROL) return status__; \
		else if (ACPI_FAILURE(status__)) return_ACPI_STATUS(status__); \
	} while(0)

static ACPI_STATUS EnumPCIDevice(u8 bus, u8 dev, PCIEnum* cb);

static ACPI_STATUS EnumPCIBus(u8 bus, PCIEnum* cb) {
	u8 dev = 0;
	//printf("acpica: Enumerating bus %02x...\n", bus);
	for (; dev < 32; dev++) {
		ACPI_RETURN_IF(EnumPCIDevice(bus, dev, cb));
	}
	return AE_OK;
}

static ACPI_STATUS EnumPCIFunction(u8 bus, u8 dev, u8 func, PCIEnum* cb) {
	u16 vendor = getVendorID(bus, dev, func);
	if (vendor == 0xffff) {
		return AE_OK;
	}
	u8 headerType = getHeaderType(bus, dev, func);
	u8 baseClass = getBaseClass(bus, dev, func);
	u8 subClass = getSubClass(bus, dev, func);
	u16 device = getDeviceID(bus, dev, func);
	ACPI_STATUS status = AE_OK;
	if (cb)
	{
		ACPI_PCI_ID id = { 0, bus, dev, func };
		cb->cur.pci_id = id;
		cb->cur.vendor = vendor;
		cb->cur.device = device;
		status = cb->cb(cb);
	}
	if (!cb || status == AE_CTRL_TERMINATE)
	{
		printf("%02x:%02x.%x: Found device %#04x:%#04x class %#x:%#x\n",
			bus, dev, func, vendor, device, baseClass, subClass);
	}
	ACPI_RETURN_IF(status);
	if (baseClass == 6 && subClass == 4)
	{
		if ((headerType & 0x7f) != 1) {
			printf("%02x:%02x.%x: Wrong header type %#x for PCI-to-PCI bridge\n",
					bus, dev, func, headerType);
			/* Just ignore this device "successfully". */
			return AE_OK;
		}
		return EnumPCIBus(getSecondaryBus(bus, dev, func), cb);
	}
	return AE_OK;
}

static ACPI_STATUS EnumPCIDevice(u8 bus, u8 dev, PCIEnum* cb) {
	u8 func = 0;
	u16 vendor = getVendorID(bus, dev, func);
	if (vendor == 0xffff) {
		return AE_OK;
	}
	const u8 maxFunc = getHeaderType(bus, dev, func) & 0x80 ? 8 : 1;
	for (func = 0; func < maxFunc; func++) {
		ACPI_RETURN_IF(EnumPCIFunction(bus, dev, func, cb));
	}
	return AE_OK;
}

ACPI_STATUS EnumeratePCI(void) {
	return EnumPCIBus(0, NULL);
}

static ACPI_STATUS FindPCIDevCB(PCIEnum* context) {
	if (context->cur.vendor == context->in.vendor &&
		context->cur.device == context->in.device) {
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

ACPI_STATUS FindPCIDevByVendor(u16 vendor, u16 device, ACPI_PCI_ID* id) {
	PCIEnum cb;
	memset(&cb, 0, sizeof(cb));
	cb.cb = FindPCIDevCB;
	cb.in.vendor = vendor;
	cb.in.device = device;
	printf("acpica: Looking for %#04x:%#04x devices...\n", vendor, device);
	ACPI_STATUS status = EnumPCIBus(0, &cb);
	if (status == AE_OK)
	{
		return AE_NOT_FOUND;
	}
	else if (status == AE_CTRL_TERMINATE)
	{
		*id = cb.cur.pci_id;
		return AE_OK;
	}
	else
	{
		return status;
	}
}
