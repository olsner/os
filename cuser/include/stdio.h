#ifndef __STDIO_H
#define __STDIO_H

#include <__decls.h>

// FIXME This shouldn't expose these things when included (since that obscures
// the real dependencies in client code) - move the implementations to a source
// file instead.
#include <sb1.h>
#include <msg_con.h>

#include <stdarg.h>

// Without -ffreestanding, our printf/vprintf generate warnings due to
// returning void instead of int. Just silence the warning :)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

__BEGIN_DECLS

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

__END_DECLS

#pragma GCC diagnostic pop

#endif /* __STDIO_H */
