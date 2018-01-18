#include <stdlib.h>
#include <sb1.h>

void abort(void) {
	for (;;) recv0(-1);
}

long int strtol(const char* p, char** end, int base) {
	return strtoul(p, end, base);
}
