#pragma once

#include "mboot.h"

namespace mem {
void init(const mboot::Info& info, u32 memory_start, u64 memory_end);
uintptr_t allocate_frame();
}
