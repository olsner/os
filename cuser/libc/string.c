#include <string.h>
#include <stdlib.h>

void* memcpy(void* dest, const void* src, size_t n) {
	asm("rep movsb": "+D"(dest), "+S"(src), "+c"(n), "=m"(dest) : : "memory");
	return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    // Non-overlapping or overlapping such that a forwards copy works.
    if (dest < src || (char*)dest > (char*)src + n) {
        return memcpy(dest, src, n);
    } else if (dest > src) {
        while (n--) {
            ((char *)dest)[n] = ((const char*)src)[n];
        }
    }
    return dest;
}

void* memset(void* dest, int c, size_t n) {
	asm("rep stosb": "+D"(dest), "+c"(n), "=m"(dest) : "a"(c) : "memory");
	return dest;
}

int memcmp(const void* a_, const void* b_, size_t n) {
	const char* a = a_, *b = b_;
	while (n--) {
		int diff = *a++ - *b++;
		if (diff) return diff;
	}
	return 0;
}

int strcmp(const char* a, const char* b) {
	while (*a && *b && *a == *b) {
		a++, b++;
	}
	return *a - *b;
}

int strncmp(const char* a, const char* b, size_t n) {
	if (!n) return 0;

	while (n-- && *a && *b && *a == *b) {
		a++, b++;
	}
	return n + 1 ? *a - *b : 0;
}

size_t strlen(const char* s) {
	size_t res = 0;
	while (*s++) res++;
	return res;
}

char* strchr(const char* s, int c) {
	while (*s && *s != c) s++;
	return *s ? (char*)s : NULL;
}

char* strstr(const char* haystack, const char *needle) {
    const size_t nlen = strlen(needle);
    const size_t hlen = strlen(haystack);
    for (size_t i = 0; i < hlen; i++)
    {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            return (char*)haystack + i;
        }
    }
    return NULL;
}

char* strcpy(char* d, const char *s) {
    return memcpy(d, s, strlen(s) + 1);
}

char* strcat(char* d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}

char* strncpy(char* d, const char *s, size_t n) {
    size_t copy = strlen(s) + 1;
    size_t fill = 0;
    if (copy > n)
    {
        copy = n;
    }
    else
    {
        fill = n - copy;
    }
    memcpy(d, s, copy);
    memset(d + copy, 0, fill);
    return d;
}

char *strdup(const char *src) {
    size_t n = strlen(src) + 1;
    return memcpy(malloc(n), src, n);
}
