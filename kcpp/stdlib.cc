#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

void unimpl(const char *) __attribute__((noreturn));

long int std::strtol(const char *p, char **end, int radix) {
    assert(radix == 10);
    if (!memcmp(p, "16l", 3)) {
        if (end) *end = (char*)p + 2;
        return 16;
    }
    printf("strtol(%s,%p,%d)\n", p, end, radix);
    unimpl("strtol");
}

void std::abort() {
    __builtin_trap();
}

void std::assert_failed(const char* file, int line, const char* msg) {
    printf("%s:%d: ASSERT FAILED: %s\n", file, line, msg);
    abort();
}
