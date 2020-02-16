#include <stdio.h>
#include <sb1.h>
#include <msg_con.h>

static const ipc_dest_t CONSOLE_HANDLE = 3; /* Hardcode galore */

void putchar(char c) {
	send1(MSG_CON_WRITE, CONSOLE_HANDLE, c);
}

char getchar(void) {
	ipc_arg_t c = 0;
	sendrcv1(MSG_CON_READ, CONSOLE_HANDLE, &c);
	return c;
}

void puts(const char* str) {
	while (*str) putchar(*str++);
	putchar('\n');
}

