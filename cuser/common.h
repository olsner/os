#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uintptr_t size_t;
typedef unsigned int uint;
// TODO(x32): Introduce types for IPC dest/src (uintptr_t) and arguments (u64)

// FIXME This causes 'start' to follow various silly calling conventions - such
// as saving callee-save registers. Find some way to get rid of that...
// Or wait for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=38534 to be fixed.
void start(void) __attribute__((noreturn,section(".start")));

// Symbols exposed by the linker script
extern char __bss_start[1];
extern char __bss_end[1];
extern char __data_lma[1];
extern char __data_lma_end[1];
extern char __data_size[1];
extern char __data_vma[1];

// Attribute for putting variable in a special placeholder section. Useful for
// reserving virtual memory space or dummy memory space for handles.
#define S__(x) #x
#define S_(x) S__(x)
#define PLACEHOLDER_SECTION __attribute__((section(".placeholder." __FILE__ "." S_(__LINE__))))
#define ALIGN(n) __attribute__((aligned(n)))

enum syscalls_builtins {
	MSG_NONE = 0,
	SYSCALL_RECV = MSG_NONE,
	MSG_MAP,
	MSG_PFAULT,
	MSG_UNMAP,
	MSG_HMOD,
	SYSCALL_WRITE = 6,
	// arg0 (dst) = port
	// arg1 = flags (i/o) and data size:
	//  0x10 = write (or with data size)
	//  0x01 = byte
	//  0x02 = word
	//  0x04 = dword
	// arg2 (if applicable) = data for output
	SYSCALL_IO = 7, // Backdoor!
	MSG_GRANT = 8,
	MSG_PULSE = 9,
	MSG_USER = 16,
};

enum msg_con {
	MSG_CON_WRITE = MSG_USER,
	MSG_CON_READ,
};

enum msg_irq {
	/**
	 * Takes one argument, the IRQ number (GSI in the case of I/O APICs).
	 * (Or does it take a number local to the interrupt controller?)
	 */
	MSG_REG_IRQ = MSG_USER,
	/**
	 * Acknowledge receipt of the interrupt.
	 *
	 * arg1: IRQ number to acknowledge
	 */
	MSG_IRQ_ACK,
};

enum msg_ethernet {
	/**
	 * Register an ethernet protocol. Use with a fresh handle, memory map pages
	 * to read incoming packets and store outgoing packets.
	 * The special protocol number 0 can be used to match all protocols.
	 *
	 * The number of buffers to use is decided by the protocol through sending
	 * receive messages for each buffer it wants to use for reception. All
	 * buffers start out owned by the protocol until given to ethernet for
	 * sending or receiving.
	 *
	 * arg1: protocol number
	 * Returns:
	 * arg1: MAC address of card
	 */
	MSG_ETHERNET_REG_PROTO = MSG_USER,
	/**
	 * protocol -> ethernet: allocate a buffer to reception, handing over
	 * ownership to the ethernet driver. Can only be sent without sendrcv. The
	 * reply comes by pulse afterwards.
	 *
	 * The buffer contains the whole ethernet frame including headers, as it
	 * came from the network. It may contain any type of frame, with or without
	 * a VLAN header.
	 *
	 * arg1: page number of the buffer to receive into
	 */
	MSG_ETHERNET_RECV,
	/**
	 * protocol -> ethernet: Send a packet. The given page number is owned by
	 * the ethernet driver until delivered over the wire and the
	 * pulse reply is sent.
	 *
	 * arg1: page number of buffer that contains data to send.
	 * arg2: datagram length
	 * Returns:
	 * arg1: page number of delivered packet - the page is no longer owned by
	 * the driver.
	 */
	MSG_ETHERNET_SEND,
};
enum
{
	ETHERTYPE_ANY = 0,
};

enum msg_timer
{
	/**
	 * Register a timer to trigger in approximately N nanoseconds.
	 *
	 * arg1: timeout.
	 */
	MSG_REG_TIMER = MSG_USER,
	/**
	 * A registered timer has triggered. The timer will not be triggered again
	 * unless you re-register a new timeout.
	 */
	MSG_TIMER_T,
	/**
	 * Two-argument sendrcv.
	 *
	 * returns:
	 * arg1: milliseconds
	 * arg2: ticks
	 */
	MSG_TIMER_GETTIME,
};

enum msg_fb
{
	/**
	 * Set video mode (size and bpp).
	 *
	 * arg1: width << 32 | height
	 * arg2: bits per pixel
	 *
	 * The user should map the handle, as much memory as needed for the given
	 * resolution. (Or more, to support use of MSG_PRESENT.)
	 */
	MSG_SET_VIDMODE = MSG_USER,
	/**
	 * For 4- or 8-bit modes, update a palette entry.
	 *
	 * arg1: index << 24 | r << 16 | g << 8 | b
	 */
	MSG_SET_PALETTE,
	/**
	 * Placeholder for future (window manager driven) api - present a frame.
	 *
	 * arg1: origin of frame to present, relative mapped memory area.
	 * (an id? some way for the client to know when this has been presented and
	 * the previous frame will not be used by the window manager again)
	 */
	MSG_PRESENT,
};

enum msg_kind {
	MSG_KIND_SEND = 0,
	MSG_KIND_CALL = 1,
	//MSG_KIND_REPLYWAIT = 2
};

enum msg_masks {
	MSG_CODE_MASK = 0x0ff,
	MSG_KIND_MASK = 0x300,
	MSG_KIND_SHIFT = 8
};

static uintptr_t msg_set_kind(uintptr_t msg, enum msg_kind kind) {
	return msg | (kind << 8);
}
static uintptr_t msg_send(uintptr_t msg) {
	return msg_set_kind(msg, MSG_KIND_SEND);
}
static uintptr_t msg_call(uintptr_t msg) {
	return msg_set_kind(msg, MSG_KIND_CALL);
}
static u8 msg_code(uintptr_t msg) {
	return msg & MSG_CODE_MASK;
}
static enum msg_kind msg_get_kind(uintptr_t msg) {
	return (msg & MSG_KIND_MASK) >> MSG_KIND_SHIFT;
}

/*
 * Tries to fit into the SysV syscall ABI for x86-64.
 * Message registers: rsi, rdx, r8, r9, r10
 * Syscall/message number in eax
 * Message registers are also return value registers. eax returns a message.
 * (rcx and r11 are used by syscall.)
*/

// syscallN: syscall with exactly one return value (some kind of error code?),
// one syscall number and N additional parameters. Argument registers are
// clobbered and not returned.
// ipcN: generic IPC (send/rcv/sendrcv/[replywait]), takes a message and
// destination/source (by ref) plus N message registers (by ref). Message
// registers are modified to the "returned" reply/message, the return value is
// the reply message number (or send syscall return code).
// recvN: ipcN for receives - message registers are undefined on syscall entry,
// on syscall exit the message register params are updated. Return value is
// received message number. source param is updated to the sender value.
// sendN: ipcN for sends - params are not by reference, only error code is
// returned.
// sendrcvN: ipcN for calls - only arguments are by reference since the reply
// must come from the same process we sent to. Returns the reply message like
// recv.


static inline uintptr_t syscall1(uintptr_t msg, uintptr_t dest) {
	uintptr_t res;
	__asm__ __volatile__ ("syscall"
		: "=a" (res),
		  /* clobbered inputs */
		  "=D" (dest)
		: "a" (msg), "D" (dest)
		: "%rsi", "%rdx", "r8", "r9", "r10", "r11", "%rcx", "memory");
	return res;
}

static inline uintptr_t syscall2(uintptr_t msg, uintptr_t arg1, uintptr_t arg2) {
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (arg1), "=S" (arg2)
		: "a" (msg), "D" (arg1), "S" (arg2)
		: "%rdx", "r8", "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline uintptr_t syscall3(uintptr_t msg, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (arg1), "=S" (arg2), "=d" (arg3)
		: "a" (msg), "D" (arg1), "S" (arg2), "d" (arg3)
		: "r8", "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline uintptr_t syscall4(uintptr_t msg, uintptr_t dest, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
	register long r8 __asm__("r8") = arg3;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (dest), "=S" (arg1), "=d" (arg2), "=r" (r8)
		: "a" (msg), "D" (dest), "S" (arg1), "d" (arg2), "r" (r8)
		: "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline uintptr_t syscall5(uintptr_t msg, uintptr_t dest, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
	register long r8 __asm__("r8") = arg3;
	register long r9 __asm__("r9") = arg4;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (dest), "=S" (arg1), "=d" (arg2), "=r" (r8), "=r" (r9)
		: "a" (msg), "D" (dest), "S" (arg1), "d" (arg2), "r" (r8), "r" (r9)
		: "r10", "r11", "%rcx", "memory");
	return msg;
}

// Send 3, receive 3, ignore r9 and r10
static inline uintptr_t ipc3(uintptr_t msg, uintptr_t* destSrc, uintptr_t* arg1, uintptr_t* arg2, uintptr_t* arg3) {
	register long r8 __asm__("r8") = *arg3;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (*destSrc), "=S" (*arg1), "=d" (*arg2), "=r" (r8)
		: "a" (msg), "D" (*destSrc), "S" (*arg1), "d" (*arg2), "r" (r8)
		: "r9", "r10", "r11", "%rcx", "memory");
	*arg3 = r8;
	return msg;
}

static inline uintptr_t ipc2(uintptr_t msg, uintptr_t* src, uintptr_t* arg1, uintptr_t* arg2)
{
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* in/outputs */
			"=D" (*src), "=S" (*arg1), "=d" (*arg2)
		: "a" (msg), "D" (*src), "S" (*arg1), "d" (*arg2)
		: "r8", "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline uintptr_t sendrcv3(uintptr_t msg, uintptr_t dst, uintptr_t* arg1, uintptr_t* arg2, uintptr_t* arg3)
{
	return ipc3(msg_call(msg), &dst, arg1, arg2, arg3);
}

static inline uintptr_t sendrcv2(uintptr_t msg, uintptr_t dst, uintptr_t* arg1, uintptr_t* arg2)
{
	return ipc2(msg_call(msg), &dst, arg1, arg2);
}

static inline uintptr_t recv3(uintptr_t* src, uintptr_t* arg1, uintptr_t* arg2, uintptr_t* arg3)
{
	register long r8 __asm__("r8");
	uintptr_t msg;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* in/outputs */
			"=D" (*src), "=S" (*arg1), "=d" (*arg2), "=r" (r8)
		: "a" (0), "D" (*src)
		: "r9", "r10", "r11", "%rcx", "memory");
	*arg3 = r8;
	return msg;
}

static inline uintptr_t recv2(uintptr_t* src, uintptr_t* arg1, uintptr_t* arg2)
{
	uintptr_t msg;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* in/outputs */
			"=D" (*src), "=S" (*arg1), "=d" (*arg2)
		: "a" (0), "D" (*src)
		: "r8", "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline uintptr_t ipc1(uintptr_t msg, uintptr_t* src, uintptr_t* arg1)
{
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* in/outputs */
			"=D" (*src), "=S" (*arg1)
		: "a" (msg), "D" (*src), "S" (*arg1)
		: "r8", "r9", "r10", "r11", "%rcx", "%rdx", "memory");
	return msg;
}

static inline uintptr_t recv1(uintptr_t* src, uintptr_t* arg1)
{
	uintptr_t msg;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* in/outputs */
			"=D" (*src), "=S" (*arg1)
		: "a" (0), "D" (*src)
		: "r8", "r9", "r10", "r11", "%rcx", "%rdx", "memory");
	return msg;
}

static inline uintptr_t recv0(uintptr_t src)
{
	return syscall1(0, src);
}

static inline uintptr_t send3(uintptr_t msg, uintptr_t dst, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3)
{
	return ipc3(msg_send(msg), &dst, &arg1, &arg2, &arg3);
}

static inline uintptr_t send2(uintptr_t msg, uintptr_t dst, uintptr_t arg1, uintptr_t arg2)
{
	return ipc2(msg_send(msg), &dst, &arg1, &arg2);
}

static inline uintptr_t send1(uintptr_t msg, uintptr_t dst, uintptr_t arg1)
{
	return syscall2(msg_send(msg), dst, arg1);
}
static inline uintptr_t send0(uintptr_t msg, uintptr_t dst)
{
	return syscall1(msg_send(msg), dst);
}

static inline uintptr_t sendrcv1(uintptr_t msg, uintptr_t dst, uintptr_t* arg1)
{
	return ipc1(msg_call(msg), &dst, arg1);
}
static inline uintptr_t sendrcv0(uintptr_t msg, uintptr_t dst)
{
	return syscall1(msg_call(msg), dst);
}

#ifndef RAW_STDIO
static const u64 CONSOLE_HANDLE = 3; /* Hardcode galore */

static void putchar(char c) {
	send1(MSG_CON_WRITE, CONSOLE_HANDLE, c);
}

static char getchar(void) {
	uintptr_t c = 0;
	sendrcv1(MSG_CON_READ, CONSOLE_HANDLE, &c);
	return c;
}
#else
static void putchar(char c) {
	syscall1(SYSCALL_WRITE, c);
}
#endif

static void puts(const char* str) {
	while (*str) putchar(*str++);
}

static void hmod(uintptr_t h, uintptr_t rename, uintptr_t copy) {
	syscall3(MSG_HMOD, h, rename, copy);
}

static void hmod_delete(uintptr_t h) {
	hmod(h, 0, 0);
}

static void hmod_rename(uintptr_t h, uintptr_t rename) {
	hmod(h, rename, 0);
}

static void hmod_copy(uintptr_t h, uintptr_t copy) {
	hmod(h, h, copy);
}

static void pulse(uintptr_t handle, u64 mask) {
	send1(MSG_PULSE, handle, mask);
}

enum prot {
	PROT_EXECUTE = 1,
	PROT_WRITE = 2,
	PROT_READ = 4,
	PROT_RWX = 7,
	MAP_ANON = 8,
	MAP_PHYS = 16,
	MAP_DMA = MAP_PHYS | MAP_ANON,
	PROT_NO_CACHE = 32,
};
// Maximum end-address of user mappings.
#if __INTPTR_WIDTH__ == 32
// TODO For x32, we might keep two limits - one "practical" (32-bit pointer
// max) and one theoretical that could be accessed from 64-bit code.
static const uintptr_t USER_MAP_MAX = UINTPTR_MAX;
#else
static const uintptr_t USER_MAP_MAX = 0x800000000000;
#endif
static void* map(uintptr_t handle, enum prot prot, const volatile void *local_addr, uintptr_t offset, uintptr_t size) {
	if (size < 0x1000) { size = 0x1000; }
	return (void*)syscall5(MSG_MAP,
		handle, prot, (uintptr_t)local_addr, offset, size);
}

static void map_anon(int prot, void *local_addr, uintptr_t size) {
	if (size) {
		map(0, MAP_ANON | prot, local_addr, 0, size);
	}
}

static void prefault(const volatile void* addr, int prot) {
	uintptr_t arg1 = (uintptr_t)addr;
	uintptr_t arg2 = prot;
	uintptr_t rcpt = 0;
	(void)ipc2(MSG_PFAULT, &rcpt, &arg1, &arg2);
}

static void prefault_range(void* start, size_t size, int prot) {
	u8* p = (u8*)start;
	u8* end = p + size;
	while (p < end) {
		prefault(p, prot);
		p += 4096;
	}
}

static void grant(uintptr_t rcpt, void* addr, int prot) {
	uintptr_t arg1 = (uintptr_t)addr;
	uintptr_t arg2 = prot;
	(void)ipc2(MSG_GRANT, &rcpt, &arg1, &arg2);
}

#define STRING_INL_LINKAGE static
#include "string.c"

static void __default_section_init(void) {
	map_anon(PROT_READ | PROT_WRITE, __bss_start, __bss_end - __bss_start);
	memcpy(__data_vma, __data_lma, (uintptr_t)&__data_size);
}

static u64 portio(u16 port, u64 flags, u64 data) {
	return syscall3(SYSCALL_IO, port, flags, data);
}

/* Not all of these are implemented, depending on what you link against. */
extern void printf(const char* fmt, ...);
extern void vprintf(const char* fmt, va_list ap);
extern void* malloc(size_t size);
extern void free(void* p);

static void abort(void) __attribute__((noreturn));
static void abort(void)
{
	for (;;) recv0(-1);
}

static void assert_failed(const char* file, int line, const char* msg) __attribute__((noreturn));
static void assert_failed(const char* file, int line, const char* msg) {
	printf("%s:%d: ASSERT FAILED (%s)\n", file, line, msg);
	abort();
}
#define assert(X) \
	do { if (!(X)) assert_failed(__FILE__, __LINE__, #X); } while (0)

static void hexdump(char* data, size_t length) {
	size_t pos = 0;
	while (pos < length) {
		printf("\n%04x: ", pos);
		for (int i = 0; i < 16 && pos < length; i++) {
			printf("%02x ", (u8)data[pos++]);
		}
	}
	printf("\n");
}

// For header type 0 (!)
enum pci_regs
{
	PCI_VENDOR_ID = 0x00,
	PCI_DEVICE_ID = 0x02,
	PCI_COMMAND   = 0x04,
	PCI_STATUS    = 0x06,
	PCI_REV_ID    = 0x08,
	PCI_PROG_IF   = 0x09,
	PCI_SUBCLASS  = 0x0a,
	PCI_CLASS     = 0x0b,
	PCI_CL_SIZE   = 0x0c,
	PCI_LATENCY_TIMER = 0x0d,
	PCI_HEADER_TYPE = 0x0e,
	PCI_BIST      = 0x0f,
	PCI_BAR_0     = 0x10,
	PCI_BAR_1     = 0x14,
	// .. BAR5
	// Cardbus CIS Pointer
	PCI_SS_VENDOR_ID = 0x2c,
	PCI_SUBSYSTEM_ID = 0x2e,
};
enum pci_command_bits
{
	PCI_COMMAND_IOSPACE = 1,
	PCI_COMMAND_MEMSPACE = 2,
	PCI_COMMAND_MASTER = 4,
};

/**
 * A simple (compiler) barrier. Memory writes to volatile variables before the
 * barrier must not be moved to after the barrier, regardless of optimizations.
 */
static void __barrier(void) {
	__asm__ __volatile__ ("":::"memory");
}

#ifdef __cplusplus
}
#endif

#endif // __COMMON_H
