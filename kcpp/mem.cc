//#include <stdlib.h>
#include <stddef.h>

extern "C" void *malloc(size_t);
extern "C" void free(void *);

void *operator new(size_t sz) {
    return malloc(sz);
}
void operator delete(void *p) {
    free(p);
}
void operator delete(void *p, size_t) {
    free(p);
}
void *operator new[](size_t sz) {
    return malloc(sz);
}
void operator delete[](void *p) {
    free(p);
}
void operator delete[](void *p, size_t) {
    free(p);
}

