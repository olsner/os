#include "common.h"

#define log printf
#if 0
#define debug log
#else
#define debug(...) (void)0
#endif

#define ALIGN(n) __attribute__((aligned(n)))

#define FRAME_DELAY 20000000/*ns*/
#define W 640UL
#define H 480UL
#define BPP 8

static const uintptr_t fbhandle = 5;
static const uintptr_t apic_handle = 6;

static u16 usqrt(u32 x) {
	u32 res = x;
	while (res * res < x) res++;
	return res;
}

static u32 diff(u32 x1, u32 x2) {
	return x1 < x2 ? x2 - x1 : x1 - x2;
}

static u16 alpha(u32 dx, u32 dy) {
	u32 res = (dx << 16) / (dy ? dy : 1);
	while (res >= 0x100) res >>= 1;
	return res;
}

static void set_palette(u8 pal) {
	debug("fbtest: set palette %u\n", pal);
	for (uint i = 0; i < 256; i++) {
		u8 r = i + pal, g = i + pal + 85, b = i + pal - 85;
		uintptr_t arg = (i << 24) | (r << 16) | (g << 8) | b;
		send1(MSG_SET_PALETTE, fbhandle, arg);
	}
}

static u8 frame_buffer[W*H*BPP/8] PLACEHOLDER_SECTION ALIGN(4096);

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

	send2(MSG_REG_TIMER, apic_handle, FRAME_DELAY, 0);
	log("fbtest: timer started\n");

	u8 *dst = frame_buffer;
	for (uint y = 0; y < H; y++) {
		for (uint x = 0; x < W; x++) {
			u32 dx = diff(x, W/2);
			u32 dy = diff(y, H/2);

#if BPP > 8
			// B, G, R
			*dst++ = alpha(dx, dy);
			*dst++ = usqrt(dx*dx + dy*dy);
			*dst++ = r;
			*dst++ = r;
#else
			// palette index
			*dst++ = usqrt(dx*dx + dy*dy);
#endif
		}
	}

	debug("fbtest: pattern drawn\n");

	u8 palette = 0;
	set_palette(palette);

	for(;;) {
		uintptr_t rcpt = 0, arg, arg2;
		uintptr_t msg = recv2(&rcpt, &arg, &arg2);
		if ((msg & 0xff) == MSG_PULSE) {
			// && rcpt == apic_handle
			set_palette(++palette);
			send2(MSG_REG_TIMER, apic_handle, FRAME_DELAY, 0);
			continue;
		}
	}
}
