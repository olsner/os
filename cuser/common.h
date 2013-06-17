#include <stdint.h>

typedef uint64_t u64;

// FIXME This causes 'start' to follow various silly calling conventions - such
// as saving callee-save registers. Find some way to get rid of that...
// Or wait for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=38534 to be fixed.
void start() __attribute__((noreturn,section(".start")));

enum syscalls_builtins {
	MSG_NONE = 0,
	SYSCALL_RECV = MSG_NONE,
	MSG_MAP,
	MSG_PFAULT,
	MSG_UNMAP,
	MSG_HMOD,
	SYSCALL_WRITE = 6,
};

enum msg_kind {
	MSG_KIND_RECV = 0,
	MSG_KIND_SEND = 1,
	MSG_KIND_CALL = 2,
	//MSG_KIND_REPLYWAIT = 3
};

static uintptr_t msg_set_kind(uintptr_t msg, enum msg_kind kind) {
	return msg | (kind << 8);
}
static uintptr_t msg_send(uintptr_t msg) {
	return msg_set_kind(msg, MSG_KIND_SEND);
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


static inline uintptr_t syscall1(uintptr_t msg, uintptr_t dest) {
	uintptr_t res;
	__asm__ __volatile__ ("syscall"
		: "=a" (res),
		  /* clobbered inputs */
		  "=D" (dest)
		: "a" (msg), "D" (dest)
		: "%rsi", "%rdx", "r8", "r9", "r10", "r11", "%rcx");
	return res;
}

static inline uintptr_t syscall2(uintptr_t msg, uintptr_t arg1, uintptr_t arg2) {
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (arg1), "=S" (arg2)
		: "a" (msg), "D" (arg1), "S" (arg2)
		: "%rdx", "r8", "r9", "r10", "r11", "%rcx");
	return msg;
}

static inline uintptr_t syscall3(uintptr_t msg, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
	__asm__ __volatile__ ("syscall"
		:	/* return value(s) */
			"=a" (msg),
			/* clobbered inputs */
			"=D" (arg1), "=S" (arg2), "=d" (arg3)
		: "a" (msg), "D" (arg1), "S" (arg2), "d" (arg3)
		: "r8", "r9", "r10", "r11", "%rcx");
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
		: "r9", "r10", "r11", "%rcx");
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
		: "r10", "r11", "%rcx");
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
		: "r9", "r10", "r11", "%rcx");
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
		: "r8", "r9", "r10", "r11", "%rcx");
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
		: "r8", "r9", "r10", "r11", "%rcx");
	return msg;
}

static inline uintptr_t recv0(uintptr_t src)
{
	return syscall1(0, src);
}

static inline uintptr_t send2(uintptr_t msg, uintptr_t dst, uintptr_t arg1, uintptr_t arg2)
{
	return ipc2(msg_send(msg), &dst, &arg1, &arg2);
}

static void putchar(char c) {
	syscall1(SYSCALL_WRITE, c);
}

static void puts(const char* str) {
	while (*str) putchar(*str++);
}

static void hmod(uintptr_t h, uintptr_t rename, uintptr_t copy) {
	syscall3(MSG_HMOD, h, rename, copy);
}
