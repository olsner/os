#include "common.h"

/* physmem service: provides the means to map any page of physical memory
 * somewhere in your local address space with whatever access.
 * Clearly a privileged service that can be used to do anything unsafe. */

void start(uintptr_t pmstart, uintptr_t pmend) {
	const uintptr_t fresh = 256;
	uintptr_t pmsize = pmend - pmstart;
	puts("physmem: init complete\n");
	(void)pmsize;
	//printf("physmem: init complete, %p..%p (%x bytes)\n", pmstart, pmend, pmsize);
	for (;;) {
		uintptr_t arg1, arg2;
		uintptr_t rcpt = fresh;
		uintptr_t msg = recv2(&rcpt, &arg1, &arg2);
		// PFAULT: source handle, offset, requested flags
		switch (msg & 0xff) {
		case MSG_PFAULT: {
			if (arg1 += pmstart >= pmend) {
				arg1 = 0;
				arg2 = 0; // actual flags = 0: no access
			}
			send2(MSG_PFAULT, rcpt, arg1, arg2);
			break;
		}
		}
		if (rcpt == fresh) {
			hmod(rcpt, 0, 0);
		}
	}
}
