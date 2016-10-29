namespace mem {

struct free_page {
    free_page *next;
};
static free_page* freelist_head;
static u32 used_pages, total_pages;

template <typename T>
T *add_byte_offset(T *p, intptr_t offset) {
    return (T*)((char*)p + offset);
}

// TODO Implement per-cpu cache of page(s).
void free(void *page) {
    if (!page) return;

    free_page *free = (free_page *)page;
    free->next = freelist_head;
    freelist_head = free;
    used_pages--;
}

void *malloc(size_t sz) {
    assert(sz <= 4096);
    free_page *res = freelist_head;
    assert(res);
    freelist_head = res->next;
    memset(res, 0, 4096);
    log(malloc, "malloc(%zu) => %p\n", sz, res);
    used_pages++;
    return res;
}

void init(const mboot::Info& info, u32 memory_start, u64 memory_end) {
    assert(info.has(mboot::MemoryMap));
    auto mmap = PhysAddr<const mboot::MemoryMapItem>(info.mmap_addr);
    auto mmap_end = add_byte_offset(mmap, info.mmap_length);
    size_t n = 0;
    while (mmap < mmap_end) {
        printf("%p: start=%#lx length=%#lx\n", mmap,
                mmap->start, mmap->length);

        if (mmap->item_type == mboot::MemoryTypeMemory) {
            uintptr_t p = mmap->start;
            const uintptr_t e = p + mmap->length;
            while (p < e) {
                if (memory_start <= p && p < memory_end) {
                    used_pages++;
                    free(PhysAddr<u8>(p));
                    n++;
                }
                p += 4096;
            }
        }

        mmap = add_byte_offset(mmap, 4 + mmap->item_size);
    }

    // We added one for each page, then "freed" it, so this should be 0
    assert(!used_pages);
    total_pages = n;
    printf("Found %zu pages for %zuMiB of memory\n", n, (n * 4 + 1023) / 1024);
}

}
