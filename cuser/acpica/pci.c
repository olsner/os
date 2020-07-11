#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "acpi.h"
#include "acpica.h"

static const bool log_enum_pci = true;
static const bool log_find_pci = true;
static const bool log_claim_pci = true;

struct PCIDevice {
	ACPI_PCI_ID id;
	u16 vendor;
	u16 device;
	// -1 for present but unclaimed PCI device
	int fd;
};

// TODO Actually use list of devices
#define MAX_PCI_DEVICES 10
static struct PCIDevice pci_devices[MAX_PCI_DEVICES];
static UINT32 num_pci_devices;

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
	bool print;
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
	//log(enum_pci, "acpica: Enumerating bus %02x...\n", bus);
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

	ACPI_PCI_ID id = { 0, bus, dev, func };
	cb->cur.pci_id = id;
	cb->cur.vendor = vendor;
	cb->cur.device = device;
	ACPI_STATUS status = cb->cb(cb);
	if (cb->print)
	{
		log(enum_pci, "%02x:%02x.%x: Found device %#04x:%#04x class %#x:%#x\n",
			bus, dev, func, vendor, device, baseClass, subClass);
	}
	ACPI_RETURN_IF(status);
	if (baseClass == 6 && subClass == 4)
	{
		if ((headerType & 0x7f) != 1) {
			log(enum_pci, "%02x:%02x.%x: Wrong header type %#x for PCI-to-PCI bridge\n",
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

static ACPI_STATUS AddPCIDevice(struct PCIEnum* context)
{
	assert(num_pci_devices < MAX_PCI_DEVICES);
	struct PCIDevice* dev = &pci_devices[num_pci_devices++];
	dev->id = context->cur.pci_id;
	dev->vendor = context->cur.vendor;
	dev->device = context->cur.device;
	dev->fd = -1;
	return AE_OK;
}

ACPI_STATUS EnumeratePCI(void) {
	struct PCIEnum cb;
	memset(&cb, 0, sizeof(cb));
	cb.cb = AddPCIDevice;
	cb.print = true;
	return EnumPCIBus(0, &cb);
}

static ACPI_STATUS FindPCIDevCB(PCIEnum* context) {
	if (context->cur.vendor == context->in.vendor &&
		context->cur.device == context->in.device) {
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

static ACPI_STATUS FindPCIDevByVendor(u16 vendor, u16 device, ACPI_PCI_ID* id) {
	PCIEnum cb;
	memset(&cb, 0, sizeof(cb));
	cb.cb = FindPCIDevCB;
	cb.in.vendor = vendor;
	cb.in.device = device;
	log(find_pci, "Looking for %#04x:%#04x devices...\n", vendor, device);
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

void FindPCIDevice(uintptr_t rcpt, uintptr_t vendor_device)
{
	ACPI_PCI_ID temp = { 0, 0, 0, 0 };
	u16 vendor = vendor_device >> 16;
	u16 device = vendor_device;
	uintptr_t addr = -1;
	ACPI_STATUS status = FindPCIDevByVendor(vendor, device, &temp);
	if (ACPI_SUCCESS(status)) {
		addr = temp.Bus << 16 | temp.Device << 3 | temp.Function;
	}
	send1(MSG_ACPI_FIND_PCI, rcpt, addr);
}

static bool PCI_ID_EQUAL(ACPI_PCI_ID a, ACPI_PCI_ID b)
{
	return a.Segment == b.Segment && a.Bus == b.Bus && a.Device == b.Device && a.Function == b.Function;
}

void ClaimPCIDevice(uintptr_t rcpt, uintptr_t addr, uintptr_t pins)
{
	addr &= 0xffff;
	ACPI_PCI_ID id = { 0, (addr >> 8) & 0xff, (addr >> 3) & 31, addr & 7 };
	log(claim_pci, "claim pci %02x:%02x.%x\n", id.Bus, id.Device, id.Function);

	struct PCIDevice* dev = NULL;
	for (UINT32 i = 0; i < num_pci_devices; i++) {
		if (PCI_ID_EQUAL(id, pci_devices[i].id)) {
			dev = &pci_devices[i];
		}
	}
	assert(dev);
	assert(dev->fd < 0);

	int irqs[4] = {0};
	for (int pin = 0; pin < 4; pin++) {
		if (!(pins & (1 << pin))) continue;

		ACPI_STATUS status = RouteIRQ(&id, 0, &irqs[pin]);
		CHECK_STATUS("RouteIRQ");
		log(claim_pci, "%02x:%02x.%x pin %d routed to IRQ %#x\n",
			id.Bus, id.Device, id.Function,
			pin, irqs[pin]);
	}

	if (pins & ACPI_PCI_CLAIM_MASTER) {
		u64 value;
		AcpiOsReadPciConfiguration(&id, PCI_COMMAND, &value, 16);
		if (!(value & PCI_COMMAND_MASTER)) {
			value |= PCI_COMMAND_MASTER;
			AcpiOsWritePciConfiguration(&id, PCI_COMMAND, value, 16);
		}
	}

	pins = (u64)irqs[3] << 48 | (u64)irqs[2] << 32 | irqs[1] << 16 | irqs[0];

	int fds[2];
	socketpair(fds);

	assert(rcpt & MSG_TX_ACCEPTFD);

	dev->fd = fds[0];
	send2(MSG_ACPI_CLAIM_PCI, MSG_TX_CLOSEFD | rcpt, fds[1], pins);
	return;

failed:
	// TODO responding currently requires that acceptfd match in sender and
	// receiver - but for sending errors we don't have an fd to send with it.
	send2(MSG_ACPI_CLAIM_PCI, rcpt, msg_dest_fd(rcpt), 0);
}

