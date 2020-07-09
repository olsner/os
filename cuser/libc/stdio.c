#include <stdio.h>
#include <sb1.h>
#include <msg_con.h>

static const ipc_dest_t CONSOLE_HANDLE = 3; /* Hardcode galore */

void putchar(char c) {
	send1(MSG_CON_WRITE, CONSOLE_HANDLE, c);
}

int getchar(void) {
	ipc_arg_t c = 0;
	ipc_msg_t res = sendrcv1(MSG_CON_READ, CONSOLE_HANDLE, &c);
	// set_errno(res) or something
	return res == MSG_CON_READ ? (int)c : -1;
}

void puts(const char* str) {
	while (*str) putchar(*str++);
	putchar('\n');
}

