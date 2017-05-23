#include <stddef.h>

#ifndef STRING_INL_LINKAGE
#define STRING_INL_LINKAGE
#endif

STRING_INL_LINKAGE void memcpy(void* dest, const void* src, size_t n) {
	asm("rep movsb": "+D"(dest), "+S"(src), "+c"(n), "=m"(dest) : : "memory");
}

STRING_INL_LINKAGE void memset(void* dest, int c, size_t n) {
	asm("rep stosb": "+D"(dest), "+c"(n), "=m"(dest) : "a"(c) : "memory");
}

STRING_INL_LINKAGE int memcmp(const void* a_, const void* b_, size_t n) {
	const char* a = a_, *b = b_;
	while (n--) {
		int diff = *a++ - *b++;
		if (diff) return diff;
	}
	return 0;
}

STRING_INL_LINKAGE int strcmp(const char* a, const char* b) {
	while (*a && *b && *a == *b) {
		a++, b++;
	}
	return *a - *b;
}

STRING_INL_LINKAGE int strncmp(const char* a, const char* b, size_t n) {
	if (!n) return 0;

	while (n-- && *a && *b && *a == *b) {
		a++, b++;
	}
	return *a - *b;
}

STRING_INL_LINKAGE size_t strlen(const char* s) {
	size_t res = 0;
	while (*s++) res++;
	return res;
}

STRING_INL_LINKAGE void strcat(char* dest, const char* src) {
	memcpy(dest + strlen(dest), src, strlen(src));
}

