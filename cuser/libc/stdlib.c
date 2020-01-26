#include <stdlib.h>
#include <sb1.h>

void abort(void) {
	for (;;) { recv0(-1); sched_yield(); }
}

long int strtol(const char* p, char** end, int base) {
	return strtoul(p, end, base);
}

int atoi(const char *nptr) {
    return strtol(nptr, NULL, 10);
}
