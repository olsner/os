#include "common.h"

static const uintptr_t apic_handle = 4;
static const uintptr_t start_delay = 1000;

static const u8 PULSE_MIN = 0;
static const u8 PULSE_MAX = 63;

void start() {
	printf("timertest: starting.\n");
	uintptr_t delay = start_delay;
	u8 pulse = PULSE_MIN;

	send2(MSG_REG_TIMER, apic_handle, delay, pulse);
	for (;;) {
		uintptr_t rcpt = 0;
		uintptr_t arg1;
		uintptr_t msg = recv1(&rcpt, &arg1);
		if ((msg & 0xff) == MSG_PULSE) {
			printf("timertest: T %x %x\n", rcpt, arg1);
			send2(MSG_REG_TIMER, apic_handle, delay *= 2, pulse = (pulse + 1) % (PULSE_MAX + 1));
		}
	}
}
