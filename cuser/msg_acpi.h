#include <msg_irq.h>

enum msg_acpi {
	/* Wrappers around PCI IRQ routing (to PIC or I/O APIC) */
	MSG_ACPI_REG_IRQ = MSG_REG_IRQ,
	MSG_ACPI_IRQ_ACK = MSG_IRQ_ACK,
	/* Find (unclaimed) PCI device.
	 *
	 * arg1: pci vendor/device
	 * arg2: index (0..)
	 * Returns:
	 * arg1: pci bus/device/function, or -1 if not found
	 *
	 * Iterate index upwards to find multiple matching PCI devices until -1 is
	 * returned.
	 */
	MSG_ACPI_FIND_PCI,
	/* Claim a PCI device for the caller.
	 *
	 * arg1: pci bus/device/function
	 * arg2: flags (etc)
	 *   low 4 bits: mask of pins to route IRQs for
	 *   bit 5: set to enable bus master if possible
	 */
	MSG_ACPI_CLAIM_PCI,
	/**
	 * Read a 32-bit word from PCI config space.
	 *
	 * arg1: upper 24 bits: pci bus/device/function
	 *       lower 8 bits: config-space address, must be 32-bit aligned.
	 * returns 32-bit value in arg1
	 */
	MSG_ACPI_READ_PCI,

	/**
	 * Wait for ACPI init and initialize debugger.
	 */
	MSG_ACPI_DEBUGGER_INIT,
	/**
	 * Add a character to the debugger command buffer.
	 *
	 * arg1: number of characters to add. Currently only 1 is supported, but
	 * more characters could be encoded in fun ways.
	 * arg2: character to add
	 */
	MSG_ACPI_DEBUGGER_BUFFER,
	/**
	 * Interpret the command currently in the command buffer, and clear the
	 * buffer.
	 */
	MSG_ACPI_DEBUGGER_CMD,
	/**
	 * Clear the command buffer without running the command in it.
	 */
	MSG_ACPI_DEBUGGER_CLR_BUFFER,

	/**
	 * Register an I/O APIC. Sent from ACPI driver to I/O APIC driver when
	 * trying to use APIC mode.
	 *
	 * arg1: I/O APIC ID 0-255
	 * arg2: Base physical address of I/O APIC
	 * arg3: Global System Interrupt base of the I/O APIC.
	 * Return:
	 * arg1: Number of interrupt sources for this I/O APIC.
	 */
	MSG_ACPI_ADD_IOAPIC,
};
// Return from MSG_ACPI_FIND_PCI when no device is found.
static const uintptr_t ACPI_PCI_NOT_FOUND = -1;
static const uintptr_t ACPI_PCI_CLAIM_MASTER = 16;

