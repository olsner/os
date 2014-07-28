#include "common.h"

#define log printf
#if 1
#define debug log
#else
#define debug(...) (void)0
#endif

#define ALIGN(n) __attribute__((aligned(n)))

#define W 640UL
#define H 480UL
#define BPP 24

static uintptr_t fbhandle = 5;
static u8 frame_buffer[W*H*BPP/8] PLACEHOLDER_SECTION ALIGN(4096);

u16 usqrt(u32 x) {
	u32 res = x;
	while (res * res < x) res++;
	return res;
}

u32 diff(u32 x1, u32 x2) {
	return x1 < x2 ? x2 - x1 : x1 - x2;
}

u16 alpha(u32 dx, u32 dy) {
	u32 res = (dx << 16) / (dy ? dy : 1);
	while (res >= 0x100) res >>= 1;
	return res;
}

void start() {
	__default_section_init();
	log("fbtest: starting...\n");
	uintptr_t arg1 = W << 32 | H;
	uintptr_t arg2 = BPP;
	sendrcv2(MSG_SET_VIDMODE, fbhandle, &arg1, &arg2);

	map(fbhandle, PROT_READ | PROT_WRITE, &frame_buffer, 0, sizeof(frame_buffer));
	prefault_range(frame_buffer, sizeof(frame_buffer), PROT_READ | PROT_WRITE);
	memset(frame_buffer, 0, sizeof(frame_buffer));
	log("fbtest: faulted and cleared frame buffer\n");

	u8 *dst = frame_buffer;
	for (uint y = 0; y < H; y++) {
		for (uint x = 0; x < W; x++) {
			u32 dx = diff(x, W/2);
			u32 dy = diff(y, H/2);

			// B, G, R
			*dst++ = alpha(dx, dy);
			uint r = usqrt(dx*dx + dy*dy);
			*dst++ = r;
			*dst++ = r;
		}
	}

	log("fbtest: pattern drawn\n");
	for(;;) recv0(fbhandle);
}
