#include "sb1.h"

#ifndef RAW_STDIO
static const ipc_dest_t CONSOLE_HANDLE = 3; /* Hardcode galore */

static void putchar(char c) {
	send1(MSG_CON_WRITE, CONSOLE_HANDLE, c);
}

static char getchar(void) {
	ipc_arg_t c = 0;
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

extern void printf(const char* fmt, ...);
extern void vprintf(const char* fmt, va_list ap);
