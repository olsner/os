#ifndef __STDIO_H
#define __STDIO_H

#include <__decls.h>
#include <stdarg.h>

// Without -ffreestanding, our printf/vprintf generate warnings due to
// returning void instead of int. Just silence the warning :)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

__BEGIN_DECLS

#ifdef RAW_STDIO
#define puts RAW_puts
#define putchar RAW_putchar
#else
char getchar(void);
#endif
void putchar(char c);
void puts(const char* str);

void printf(const char* fmt, ...);
void vprintf(const char* fmt, va_list ap);

__END_DECLS

#pragma GCC diagnostic pop

#endif /* __STDIO_H */
