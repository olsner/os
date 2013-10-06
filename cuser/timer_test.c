#include "common.h"

static const uintptr_t apic_handle = 4;
static const uintptr_t start_delay = 1000;

void start() {
	printf("timertest: starting.\n");
	uintptr_t delay = start_delay;

	send1(MSG_REG_TIMER, apic_handle, delay);
	for (;;) {
		uintptr_t rcpt = apic_handle;
		uintptr_t arg1;
		uintptr_t msg = recv1(&rcpt, &arg1);
		if ((msg & 0xff) == MSG_TIMER_T) {
			printf("timertest: T\n");
			send1(MSG_REG_TIMER, apic_handle, delay *= 2);
		}
	}
}
