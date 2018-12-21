extern "C" int putchar(int c);

// Mostly dummy implementations since xprintf uses libc
struct FILE {};
static FILE* const stdout = NULL;
static FILE* const stderr = NULL;
void flockfile(FILE *) {}
void funlockfile(FILE *) {}
void fflush(FILE *) {}
ssize_t fwrite_unlocked(const void* p, size_t sz, size_t n, FILE *) {
    size_t c = n * sz;
    const char *str = (char*)p;
    while (c--) putchar(*str++);
    return n;
}
void fputc_unlocked(char c, FILE *) {
    putchar(c);
}
