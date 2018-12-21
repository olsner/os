#include <stddef.h>
#include <stdarg.h>

int putchar(int c);
void puts(const char *s);

// Mostly dummy implementations since xprintf uses libc
struct FILE {};
static FILE* const stdout = NULL;
static FILE* const stderr = NULL;
inline void flockfile(FILE *) {}
inline void funlockfile(FILE *) {}
inline void fflush(FILE *) {}
size_t fwrite_unlocked(const void* p, size_t sz, size_t n, FILE *);
inline void fputc_unlocked(char c, FILE *) {
    putchar(c);
}

void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void fprintf(FILE* fp, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void vfprintf(FILE* file, const char* fmt, va_list ap);

