#include <stddef.h>

// FIXME These are all implemented in cuser/string.c too. Should unify that
// (there might be a klibc and a user libc, but the shared bits should be
// shared at least...)

#ifndef STRING_INL_LINKAGE
#define STRING_INL_LINKAGE UNUSED
#endif

STRING_INL_LINKAGE void memcpy(void* dest, const void* src, size_t n) {
	asm("rep movsb": "+D"(dest), "+S"(src), "+c"(n), "=m"(dest) : : "memory");
}

STRING_INL_LINKAGE void memmove(void* dest, const void* src, size_t n) {
	if (dest < src) {
		memcpy(dest, src, n);
	} else {
		dest = (char *)dest + n;
		src = (char *)src + n;
		asm("std; rep movsb; cld"
			: "+D"(dest), "+S"(src), "+c"(n), "=m"(dest)
			:
			: "memory");
	}
}

STRING_INL_LINKAGE void memset(void* dest, int c, size_t n) {
	asm("rep stosb": "+D"(dest), "+c"(n), "=m"(dest) : "a"(c) : "memory");
}

STRING_INL_LINKAGE int memcmp(const void* a_, const void* b_, size_t n) {
	const char* a = (const char *)a_, *b = (const char *)b_;
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

STRING_INL_LINKAGE size_t strlen(const char* s) {
	size_t res = 0;
	while (*s++) res++;
	return res;
}

STRING_INL_LINKAGE void strcat(char* dest, const char* src) {
	memcpy(dest + strlen(dest), src, strlen(src));
}

STRING_INL_LINKAGE char* strchr_(const char* str, char c) {
	while (*str && *str != c) str++;
	return *str ? (char*)str : NULL;
}

#ifndef __cplusplus
#define strchr strchr_
#else
STRING_INL_LINKAGE const char* strchr(const char* str, char c) {
	return strchr_(str, c);
}
STRING_INL_LINKAGE char* strchr(char* str, char c) {
	return strchr_(str, c);
}
#endif
