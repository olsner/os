#include "common.h"
#include "acpica.h"

static char heap[1048576];
static char* heap_end = heap;

void init_heap(void) {
	printf("init_heap: %x..%x\n", heap, heap + sizeof(heap));
}

#if 0
#define xprintf printf
#else
#define xprintf(...) (void)0
#endif

// The crummiest malloc in the west

void* malloc(size_t size) {
	xprintf("malloc %x\n", size);
	size = (size + 2 * sizeof(size) - 1) & ~(sizeof(size) - 1);
	char* new_heap_end = heap_end + size;
	if (new_heap_end >= heap + sizeof(heap)) {
		printf("OUT OF MEMORY! size=%x\n", size);
		return NULL;
	} else {
		void* res = heap_end;
		((size_t*)new_heap_end)[-1] = size;
		heap_end = new_heap_end;
		xprintf("malloc %x => %x\n", size, res);
		return res;
	}
}

void free(void* ptr) {
	if (!ptr) {
		return;
	}
	if ((char*)ptr < heap || (char*)ptr >= heap_end) {
		printf("Invalid pointer freed!\n");
		return;
	}
	char* start_of_tail = heap_end - ((size_t*)heap_end)[-1];
	if (ptr == start_of_tail) {
		heap_end = start_of_tail;
	} else {
		xprintf("free leaves hole at %x (< %x < %x)\n", ptr, start_of_tail, heap_end);
		// What?
	}
}
