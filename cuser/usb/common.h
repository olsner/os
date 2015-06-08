#include "../common.h"

/* A "controller" provides 0 or more buses, this defines the interface both for
 * the controller and its buses. */
enum usb_controller_msg
{
	/**
	 * Sent from a recently activated controller to notify the USB system about
	 * newly found buses. Should only be sent once for each controller instance.
	 *
	 * The handle will be duplicated for each bus, and this message will be sent
	 * to associate the handle with a bus.
	 *
	 * from controller:
	 * arg1 = (max.) number of buses available from this controller.
	 * from USB system:
	 * arg1 = bus number (starting at bus 0)
	 */
	MSG_USB_CONTROLLER_INIT = MSG_USER,
	/**
	 * The bus has found a device. The device will be addressable as device
	 * 0 until it's assigned an address with ADDR_DEVICE.
	 */
	MSG_USB_NEW_DEVICE,
	/**
	 * Send a control message to a device. Must be sendrcv'd.
	 * (TODO define asynchronous variant, with a pulse and (e.g.) relevant
	 * data stored inside the buffer, or with a separate pickup message.)
	 *
	 * arg1:
	 *  bit 0..7: device address (0 for the unregistered device)
	 *  bit 8..11: endpoint (0..15)
	 *  bit 12: direction (for non-0 endpoints): 0 = OUT, 1 = IN
	 *  bit 13: (for endpoint 0): 1 if there is a data stage, direction decides
	 *    which direction
	 *  bit 14: immediate data
	 *  bit 16..18: transfer type
	 *  bit 32..47: length, 1..4096 (only single page transfers are allowed)
	 * arg2 =
	 *   if immediate and output direction: inline data (8 bytes)
	 *   otherwise: index of buffer to send from or receive to
	 * return:
	 * arg1: slot number, status?, length of received data
	 * arg2 = if input direction: inline data
	 */
	MSG_USB_TRANSFER,
	/**
	 * Finish enumerating the device at addr 0 on the bus, and give it a new
	 * address.
	 *
	 * arg1 = slot number
	 * return:
	 * arg1 = address
	 */
	MSG_ADDR_DEVICE,
};

union usb_transfer_arg
{
	struct {
		u16 addr_ep_flags;
		u8 /*usb_transfer_type*/ type;
		u8 pad1;
		u16 length;
		u16 pad2;
	} s;
	u64 i;
};

// Relative to the arg1 qword for USB_TRANSFER*
enum usb_transfer_flags
{
	UTF_SetupHasData = 1 << 13,
	UTF_ImmediateData = 1 << 14,
};

enum usb_transfer_type
{
	/**
	 * Data on a normal endpoint.
	 */
	UTT_Normal,
	/**
	 * The data is in the xHCI format for Setup Stage Control TRBs:
	 * bits 0..7: bmRequestType
	 * bits 8..15: bRequest
	 * bits 16..31: wValue
	 * bits 32..47: wIndex
	 * bits 48..63: wLength
	 *
	 * The transfer length is always 8.
	 *
	 * The Transfer Type flags are valid for this transfer only.
	 */
	UTT_ControlSetup,
	/**
	 * Data for a data stage of a control transaction, if applicable.
	 *
	 * May be chained with normal transfers, except that immediate data
	 * TRBs cannot be chained.
	 */
	UTT_ControlData,
	/**
	 * Finish a control transaction after sending data.
	 *
	 * The direction must be the opposite of the data stage, or IN if there
	 * was no data stage.
	 */
	UTT_ControlStatus,
	/**
	 * Isochronous, etc.
	 */
	UTT_Isoch,
	//UTT_NoOp
};

// Maybe bad name? For communication between USB class/device drivers and the
// "main" USB system, which wrangles the host controller(s).
enum usb_device_msg
{
	/**
	 * Find an unopened device with a given class or vendor:product. If
	 * last-device-id is given, continue enumerating after the given device.
	 *
	 * arg1: class/vendor/product (encoded)
	 * arg2: enumeration last-device-id (or 0)
	 * returns:
	 * arg1: If found: bus/device/etc identifier. Opaque 64-bit non-zero id.
	 * If not found: 0
	 */
	USB_FIND_DEVICE,
	/**
	 * Open a device. Should be called on a fresh handle.
	 *
	 * arg1: device id to open
	 *
	 * TODO: should probably return some basic device info too?
	 */
	USB_OPEN_DEVICE,
};
