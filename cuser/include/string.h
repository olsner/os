#include <__decls.h>
#include <stddef.h>

__BEGIN_DECLS
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int memcmp(const void* a_, const void* b_, size_t n);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
size_t strlen(const char* s);
char *strcat(char* dest, const char* src);
char* strchr(const char* s, int c);
__END_DECLS
