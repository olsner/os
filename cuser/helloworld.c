#include <stdint.h>

// FIXME This causes 'start' to follow various silly calling conventions - such
// as saving callee-save registers. Find some way to get rid of that...
// Or wait for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=38534 to be fixed.
void start(void) __attribute__((noreturn,section(".start")));

enum syscalls_builtins {
	SYSCALL_WRITE = 6,
};

/*
; Tries to fit into the SysV syscall ABI for x86-64.
; Message registers: rsi, rdx, r8, r9, r10
; (rcx and r11 are used by syscall.)
*/

static inline uintptr_t ipc0(uintptr_t msg, uintptr_t dest) {
	uintptr_t res;
	__asm__ __volatile__ ("syscall"
		: "=a" (res),
		  /* clobbered inputs */
		  "=D" (dest)
		: "a" (msg), "D" (dest)
		: "%rsi", "%rdx", "r8", "r9", "r10", "r11", "%rcx");
	return res;
}

static inline uintptr_t ipc3(uintptr_t msg, uintptr_t dest, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
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


static void putchar(char c) {
	ipc0(SYSCALL_WRITE, c);
	//ipc1(SYSCALL_WRITE, c, 1);
	//ipc2(SYSCALL_WRITE, c, 1, 2);
	ipc3(SYSCALL_WRITE, c, 1, 2, 3);
	//ipc4(SYSCALL_WRITE, c, 1, 2, 3, 4);
}

static void puts(const char* str) {
	while (*str) putchar(*str++);
}

void start() {
	for(;;)
		puts("hello world\n");
}
