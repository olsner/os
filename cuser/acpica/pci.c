#include "common.h"
#include "acpi.h"

static void EnumPCIDevice(u8 bus, u8 dev);

static UINT32 getPCIConfig(u8 bus, u8 dev, u8 func, u8 offset, UINT32 width)
{
	UINT64 ret;
	ACPI_PCI_ID id = { 0, bus, dev, func };
	AcpiOsReadPciConfiguration(&id, offset, &ret, width);
	return ret;
}

#define getVendorID(b,d,f) getPCIConfig(b,d,f, 0, 16)
#define getDeviceID(b,d,f) getPCIConfig(b,d,f, 2, 16)
#define getHeaderType(b,d,f) getPCIConfig(b,d,f, 14, 8)
#define getSubClass(b,d,f) getPCIConfig(b,d,f, 10, 8)
#define getBaseClass(b,d,f) getPCIConfig(b,d,f, 11, 8)
#define getSecondaryBus(b,d,f) getPCIConfig(b,d,f, 0x19, 8)

void EnumPCIBus(u8 bus) {
	u8 dev = 0;
	printf("Enumerating bus %02x...\n", bus);
	for (; dev < 32; dev++) {
		EnumPCIDevice(bus, dev);
	}
}

static void EnumPCIFunction(u8 bus, u8 dev, u8 func) {
	u16 vendor = getVendorID(bus, dev, func);
	if (vendor == 0xffff) {
		return;
	}
	u8 headerType = getHeaderType(bus, dev, func);
	u8 baseClass = getBaseClass(bus, dev, func);
	u8 subClass = getSubClass(bus, dev, func);
	printf("%02x:%02x.%x: Found device %#x:%#x class %#x:%#x\n", bus, dev, func,
			vendor,
			getDeviceID(bus, dev, func),
			baseClass, subClass);
	if (baseClass == 6 && subClass == 4)
	{
		if ((headerType & 0x7f) != 1) {
			printf("%02x:%02x.%x: Wrong header type %#x for PCI-to-PCI bridge\n",
					bus, dev, func, headerType);
			return;
		}
		EnumPCIBus(getSecondaryBus(bus, dev, func));
	}
}

static void EnumPCIDevice(u8 bus, u8 dev) {
	u8 func = 0;
	u16 vendor = getVendorID(bus, dev, func);
	if (vendor == 0xffff) {
		return;
	}
	const u8 maxFunc = getHeaderType(bus, dev, func) & 0x80 ? 8 : 1;
	for (func = 0; func < maxFunc; func++) {
		EnumPCIFunction(bus, dev, func);
	}
}

void EnumeratePCI() {
	EnumPCIBus(0);
}

