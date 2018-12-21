#pragma once

#include <stdint.h>

static const intptr_t kernel_base = -(1 << 30);

template <class T>
static constexpr T* PhysAddr(uintptr_t phys) {
    return (T*)(phys + kernel_base);
}
template <class T>
static constexpr T* HighAddr(T* lowptr) {
    return PhysAddr<T>((uintptr_t)lowptr);
}
inline uintptr_t ToPhysAddr(const volatile void *p) {
    return (uintptr_t)p - kernel_base;
}

