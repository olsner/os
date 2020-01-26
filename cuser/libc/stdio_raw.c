#include <stdio.h>
#include <sb1.h>

void putchar(char c) {
	syscall1(SYS_WRITE, c);
}
void puts(const char* str) {
	while (*str) putchar(*str++);
	putchar('\n');
}
