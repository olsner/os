#include <stddef.h>
#include <cstdlib>

using namespace std;

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

