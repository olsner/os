#include <stddef.h>
#include <cstdio>
#include <cstring>
#include "addr.h"

#define DEBUGCON 1

typedef uint16_t u16;
typedef uint8_t u8;

static void memset16(u16* dest, u16 value, size_t n) {
    if (/* constant(value) && */ (value >> 8) == (value & 0xff)) {
        std::memset(dest, value, n * 2);
    } else {
        // explicit rep movsw?
        while (n--) *dest++ = value;
    }
}

static void debugcon_putc(char c) {
#if DEBUGCON
    asm("outb %0,%1"::"a"(c),"d"((u16)0xe9));
#endif
}

namespace Console {
    static u16* const buffer = PhysAddr<u16>(0xb80a0);
    static u16 pos;
    static const u8 width = 80, height = 24;

#if 0
    void clear() {
        pos = 0;
        memset16(buffer, 0, width * height);
    }
#endif

    void write(char c) {
        if (c == '\n') {
            u8 fill = width - (pos % width);
            while(fill--) buffer[pos++] = 0;
        } else {
            buffer[pos++] = 0x0700 | c;
        }
        debugcon_putc(c);
        if (pos == width * height) {
            std::memmove(buffer, buffer + width, sizeof(*buffer) * width * (height - 1));
            pos -= width;
            memset16(buffer + pos, 0, width);
        }
    }
}

int std::putchar(int c) {
    Console::write(c);
    return 0;
}

void std::puts(const char *s) {
    while (char c = *s++) putchar(c);
    putchar('\n');
}

size_t std::fwrite_unlocked(const void* p, size_t sz, size_t n, FILE *) {
    size_t c = n * sz;
    const char *str = (char*)p;
    while (c--) putchar(*str++);
    return n;
}
