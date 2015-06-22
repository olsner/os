#include "common.h"

static u64 dma_map(const volatile void* addr, size_t size) {
	const enum prot flags = MAP_DMA | PROT_READ | PROT_WRITE | PROT_NO_CACHE;
	return (u64)map(0, flags, addr, 0, size);
}
#define dma_map(obj) dma_map(&(obj), sizeof(obj))

#define MAX_DMA_BUFFERS 16
static u8 dma_buffer_space[MAX_DMA_BUFFERS][4096] PLACEHOLDER_SECTION ALIGN(4096);
typedef struct dma_buffer {
	u64 phys; // 0 for unmapped buffers
} dma_buffer;
static dma_buffer dma_buffers[MAX_DMA_BUFFERS];
typedef struct dma_buffer_ref {
	u64 phys;
	u8 *virtual;
} dma_buffer_ref;
static dma_buffer_ref allocate_dma_buffer() {
	dma_buffer_ref res = { 0, NULL };
	for (unsigned i = 0; i < MAX_DMA_BUFFERS; i++) {
		dma_buffer *buf = dma_buffers + i;
		if (buf->phys & 1) continue;

		if (!buf->phys) {
			buf->phys = dma_map(dma_buffer_space[i]);
		}
		res.phys = buf->phys;
		res.virtual = dma_buffer_space[i];
		memset(res.virtual, 0, 4096);

		buf->phys |= 1;
		return res;
	}
	assert(!"Ran out of DMA buffers...");
	return res;
}
static void free_dma_buffer(u64 phys) {
	for (unsigned i = 0; i < MAX_DMA_BUFFERS; i++) {
		dma_buffer *buf = dma_buffers + i;
		if (buf->phys != (phys | 1)) continue;
		buf->phys ^= 1;
		return;
	}
}
