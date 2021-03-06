#ifndef _SB1_H_
#define _SB1_H_

#include <stdint.h>
#include <stddef.h>

typedef uintptr_t ipc_dest_t;
typedef uint64_t ipc_arg_t;
// Only 10-bit or so, really, since the "kind" is bit 8 and 9
// But also used for return values (errors, presumably)
typedef intptr_t ipc_msg_t;

typedef int64_t off_t; // -> sys/types.h

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

static ipc_msg_t msg_set_kind(ipc_msg_t msg, enum msg_kind kind) {
	return msg | (kind << 8);
}
static ipc_msg_t msg_send(ipc_msg_t msg) {
	return msg_set_kind(msg, MSG_KIND_SEND);
}
static ipc_msg_t msg_call(ipc_msg_t msg) {
	return msg_set_kind(msg, MSG_KIND_CALL);
}
static uint8_t msg_code(ipc_msg_t msg) {
	return msg & MSG_CODE_MASK;
}
static enum msg_kind msg_get_kind(ipc_msg_t msg) {
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

// TODO Generate these with a script or something :)

static inline int64_t syscall1(uint64_t msg, uint64_t dest) {
	uint64_t res;
	__asm__ __volatile__ ("syscall"
		: "=a" (res),
		  /* clobbered inputs */
		  "=D" (dest)
		: "a" (msg), "D" (dest)
		: "%rsi", "%rdx", "r8", "r9", "r10", "r11", "%rcx", "memory");
	return res;
}

static inline int64_t syscall2(uint64_t msg, uint64_t arg1, uint64_t arg2) {
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (arg1), "=S" (arg2)
		: "a" (msg), "D" (arg1), "S" (arg2)
		: "%rdx", "r8", "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline int64_t syscall3(uint64_t msg, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (arg1), "=S" (arg2), "=d" (arg3)
		: "a" (msg), "D" (arg1), "S" (arg2), "d" (arg3)
		: "r8", "r9", "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline int64_t syscall4(uint64_t msg, uint64_t dest, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
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

static inline int64_t syscall5(uint64_t msg, uint64_t dest, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
	register int64_t r8 __asm__("r8") = arg3;
	register int64_t r9 __asm__("r9") = arg4;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (dest), "=S" (arg1), "=d" (arg2), "=r" (r8), "=r" (r9)
		: "a" (msg), "D" (dest), "S" (arg1), "d" (arg2), "r" (r8), "r" (r9)
		: "r10", "r11", "%rcx", "memory");
	return msg;
}

static inline ipc_msg_t ipc4(ipc_msg_t msg, ipc_dest_t* destSrc, ipc_arg_t* arg1, ipc_arg_t* arg2, ipc_arg_t* arg3, ipc_arg_t* arg4) {
	register int64_t r8 __asm__("r8") = *arg3;
	register int64_t r9 __asm__("r9") = *arg4;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (*destSrc), "=S" (*arg1), "=d" (*arg2), "=r" (r8), "=r" (r9)
		: "a" (msg), "D" (*destSrc), "S" (*arg1), "d" (*arg2), "r" (r8), "r" (r9)
		: "r10", "r11", "%rcx", "memory");
	*arg3 = r8;
	*arg4 = r9;
	return msg;
}

// Send 3, receive 3, ignore r9 and r10
static inline ipc_msg_t ipc3(ipc_msg_t msg, ipc_dest_t* destSrc, ipc_arg_t* arg1, ipc_arg_t* arg2, ipc_arg_t* arg3) {
	register int64_t r8 __asm__("r8") = *arg3;
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

static inline ipc_msg_t ipc2(ipc_msg_t msg, ipc_dest_t* src, ipc_arg_t* arg1, ipc_arg_t* arg2)
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

static inline uintptr_t ipc1(uintptr_t msg, ipc_dest_t* src, ipc_arg_t* arg1)
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

static inline uintptr_t sendrcv3(ipc_msg_t msg, ipc_dest_t dst, ipc_arg_t* arg1, ipc_arg_t* arg2, ipc_arg_t* arg3)
{
	return ipc3(msg_call(msg), &dst, arg1, arg2, arg3);
}

static inline ipc_msg_t sendrcv2(ipc_msg_t msg, ipc_dest_t dst, ipc_arg_t* arg1, ipc_arg_t* arg2)
{
	return ipc2(msg_call(msg), &dst, arg1, arg2);
}

static inline ipc_msg_t recv4(ipc_dest_t* src, ipc_arg_t* arg1, ipc_arg_t* arg2, ipc_arg_t* arg3, ipc_arg_t* arg4)
{
	register int64_t r8 __asm__("r8");
	register int64_t r9 __asm__("r9");
	uintptr_t msg;
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* in/outputs */
			"=D" (*src), "=S" (*arg1), "=d" (*arg2), "=r" (r8), "=r" (r9)
		: "a" (0), "D" (*src)
		: "r10", "r11", "%rcx", "memory");
	*arg3 = r8;
	*arg4 = r9;
	return msg;
}

static inline ipc_msg_t recv3(ipc_dest_t* src, ipc_arg_t* arg1, ipc_arg_t* arg2, ipc_arg_t* arg3)
{
	register int64_t r8 __asm__("r8");
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

static inline uintptr_t recv2(uintptr_t* src, ipc_arg_t* arg1, ipc_arg_t* arg2)
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

static inline uintptr_t recv1(uintptr_t* src, ipc_arg_t* arg1)
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

static inline uintptr_t send4(uintptr_t msg, ipc_dest_t dst, ipc_arg_t arg1, ipc_arg_t arg2, ipc_arg_t arg3, ipc_arg_t arg4)
{
	return ipc4(msg_send(msg), &dst, &arg1, &arg2, &arg3, &arg4);
}

static inline uintptr_t send3(uintptr_t msg, ipc_dest_t dst, ipc_arg_t arg1, ipc_arg_t arg2, ipc_arg_t arg3)
{
	return ipc3(msg_send(msg), &dst, &arg1, &arg2, &arg3);
}

static inline uintptr_t send2(uintptr_t msg, ipc_dest_t dst, ipc_arg_t arg1, ipc_arg_t arg2)
{
	return ipc2(msg_send(msg), &dst, &arg1, &arg2);
}

static inline uintptr_t send1(uintptr_t msg, ipc_dest_t dst, ipc_arg_t arg1)
{
	return syscall2(msg_send(msg), dst, arg1);
}
static inline uintptr_t send0(uintptr_t msg, ipc_dest_t dst)
{
	return syscall1(msg_send(msg), dst);
}

static inline uintptr_t sendrcv1(uintptr_t msg, ipc_dest_t dst, ipc_arg_t* arg1)
{
	return ipc1(msg_call(msg), &dst, arg1);
}
static inline uintptr_t sendrcv0(uintptr_t msg, ipc_dest_t dst)
{
	return syscall1(msg_call(msg), dst);
}


/*****************************************************************************/
/* Syscall wrappers. */
/*****************************************************************************/

#include "msg_syscalls.h"

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

static uintptr_t pulse(uintptr_t handle, uint64_t mask) {
	return send1(MSG_PULSE, handle, mask);
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

static int64_t map_raw(ipc_dest_t handle, int prot, uint64_t addr, uint64_t offset, uint64_t size) {
	return syscall5(MSG_MAP, handle, prot, addr, offset, size);
}

// Maximum end-address of user mappings.
#if __INTPTR_WIDTH__ == 32
// TODO For x32, we might keep two limits - one "practical" (32-bit pointer
// max) and one theoretical that could be accessed from 64-bit code.
static const uintptr_t USER_MAP_MAX = UINTPTR_MAX;
#else
static const uintptr_t USER_MAP_MAX = 0x800000000000;
#endif
static void* map(ipc_dest_t handle, enum prot prot, const volatile void *local_addr, off_t offset, size_t size) {
	if (size < 0x1000) { size = 0x1000; }
	return (void*)(intptr_t)map_raw(handle, prot, (uintptr_t)local_addr, offset, size);
}

static void map_anon(int prot, void *local_addr, uintptr_t size) {
	if (size) {
		map(0, MAP_ANON | prot, local_addr, 0, size);
	}
}

static uint64_t map_dma(int prot, const volatile void *local_addr, size_t size) {
	if (size) {
		return map_raw(0, MAP_DMA | prot, (uintptr_t)local_addr, 0, size);
	} else {
		return (uint64_t)-1;
	}
}

static void prefault(const volatile void* addr, int prot) {
	syscall3(MSG_PFAULT, 0, (uintptr_t)addr, prot);
}

static int grant(uintptr_t rcpt, const volatile void* addr, int prot) {
	return syscall3(MSG_GRANT, rcpt, (uintptr_t)addr, prot);
}

static uint64_t portio(uint16_t port, uint64_t flags, uint64_t data) {
	return syscall3(SYSCALL_IO, port, flags, data);
}

#endif /* _SB1_H_ */
