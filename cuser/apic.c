#include "common.h"

#define printf(...) (void)0

static const uintptr_t irq_driver = 1;
static const uintptr_t fresh_handle = 100;
static const uintptr_t apic_pbase = 0xfee00000;
// Very approximate ticks per second for qemu
#define APIC_TICKS 6000000
static const u32 apic_ticks = APIC_TICKS;

static const u8 apic_timer_irq = 48;

#define REG(name, offset) \
	static const size_t name = (offset) / sizeof(u32)

REG(TPR, 0x80);
REG(EOI, 0xb0);
REG(SPURIOUS, 0xf0);
enum SpuriousReg {
	APIC_SOFTWARE_ENABLE = 0x100,
};
REG(TIMER_LVT, 0x320);
enum {
	// Bit 17: TMM
	TIMER_TMM_PERIODIC = 0x20000,
	TIMER_TMM_ONESHOT = 0,
};
REG(PERFC_LVT, 0x340);
REG(LINT0_LVT, 0x350);
REG(LINT1_LVT, 0x360);
REG(ERROR_LVT, 0x370);
enum {
	// TODO reverse the values used for LVT registers and define some nice
	// symbolic constants...
	LVT_TMM = 0x20000, // TiMerMode
	LVT_MASK = 0x10000,

	LVT_TGM = 0x8000,
	LVT_TRIGGER_MODE = LVT_TGM,
	LVT_TGM_LEVEL = LVT_TGM,
	LVT_TGM_EDGE = 0,

	LVT_MT_MASK = 0x700,
	LVT_MT_FIXED = 0x000,
	LVT_MT_EXT = 0x700,
	LVT_MT_NMI = 0x400,
	//0x08700 (LINT0)
	//0x00400 (LINT1)
};
REG(APICTIC, 0x380); // Timer Initial Count?
REG(APICTCC, 0x390); // Timer Current Count?
REG(TIMER_DIV, 0x3e0);
enum TimerDiv {
	TIMER_DIV_128 = 10,
};

typedef struct timer timer;
struct timer {
	u64 timeout;
	timer* down;
	timer* right;
	u8 pulse;
};

timer* ph_merge(timer* l, timer* r) {
	if (!l) {
		assert(!r->right);
		return r;
	}
	if (!r) {
		assert(!l->right);
		return l;
	}
	assert(!l->right && !r->right);
	if (r->timeout < l->timeout) {
		timer* t = r;
		r = l;
		l = t;
	}
	// l <= r!

	// Insert r first in l's down-list.
	r->right = l->down;
	l->down = r;
	return l;
}
timer* ph_mergepairs(timer* l) {
	if (!l || !l->right) return l;

	timer* r = l->right;
	timer* hs = r->right;
	l->right = r->right = NULL;
	assert(hs != l && hs != r && r != l);
	// FIXME recursion...
	// We can use l->right after merge returns, since merge() always returns
	// something with a right-value of NULL. l will never be touched until
	// we're about to merge it with the result of mergePairs
	l = ph_merge(l,r);
	hs = ph_mergepairs(hs);
	return ph_merge(l,hs);
}
timer* timer_pop(timer** head) {
	timer* res = *head;
	if (!res) return NULL;

	assert(!res->right);
	timer* l = res->down;
	res->down = NULL;
	*head = ph_mergepairs(l);
	return res;
}
timer* timer_add(timer** head, timer* timer) {
	*head = ph_merge(*head, timer);
	assert(!(*head)->right);
	return timer;
}

static volatile u32 apic[1024] PLACEHOLDER_SECTION ALIGN(4096);

static timer* timers_head;
// one MILLION timers
#define MAX_TIMERS (1048576)
static timer timer_heap[MAX_TIMERS];
static u32 timer_heap_limit = 0;
static timer* free_timers;
static u32 prevTIC = 0;
static struct {
	u64 tick_counter;
	u64 ms_counter;
	u64 ns_counter;
	// pad to a full page to avoid exposing anything we don't have to
	u64 padding[510];
} static_data;

static u64 ticks_to_millis(u64 ticks) {
	return ticks / (apic_ticks / 1000);
}
static u64 ticks_to_nanos(u64 ticks) {
	return 500 * ticks / (apic_ticks / 2000000);
}

u64 get_tick_counter() {
	// Since this is a count*down* timer, the elapsed count is the initial count
	// minus the current count.
	u32 tcc = apic[APICTCC];
	printf("apic: from tic %u to tcc %u: %u elapsed\n", prevTIC, tcc, prevTIC - tcc);
	static_data.tick_counter += prevTIC - tcc;
	static_data.ms_counter = ticks_to_millis(static_data.tick_counter);
	prevTIC = tcc;
	return static_data.tick_counter;
}
#define MIN(x,y) ((x) > (y) ? (y) : (x))
#define MAX_U32(x) MIN(x, (u32)-1)
void setTIC(u64 ticks) {
	ticks = MAX_U32(ticks);
	// When we reset TIC we lose track of how much time passed between TIC..TCC
	get_tick_counter();
	prevTIC = ticks;
	apic[APICTIC] = ticks;
	printf("apic: setTIC %u\n", ticks);
}

timer* timer_alloc() {
	if (free_timers) {
		return timer_pop(&free_timers);
	} else {
		return &timer_heap[timer_heap_limit++];
	}
}
timer* timer_new(u64 timeout, u8 pulse) {
	timer* t = timer_alloc();
	t->timeout = timeout;
	t->pulse = pulse;
	return t;
}
void timer_free(timer* t) {
	t->timeout = (uintptr_t)t;
	timer_add(&free_timers, t);
}
timer* reg_timer(u64 ns, u8 pulse) {
	u64 ticks = (ns / 1000) * apic_ticks / 1000000;
	printf("apic: %lu ns -> %lu ticks\n", ns, ticks);
	u64 tick_counter = get_tick_counter();
	u64 tick_timeout = tick_counter + ticks;
	timer* t = timer_add(&timers_head, timer_new(tick_timeout, pulse));
	printf("apic: registered %p\n", t);
	return t;
}

static void __more_stack(size_t more) {
	static const uintptr_t TOP = 0x100000;
	static const size_t START = 0x1000;
	static const size_t MAX = TOP - START;
	static size_t extra;
	assert(!(more % 0x1000));
	printf("Extending stack... had %ld want %ld more\n", extra, more);
	extra += more;
	assert(extra <= MAX);
	char* bottom = (char*)TOP - START - extra;
	printf("Extending stack... adding %p..%p\n", bottom, bottom + more);
	map_anon(PROT_READ | PROT_WRITE, bottom, more);
	printf("Extending stack... bottom now %p\n", bottom);
}

// Fun stuff: APIC is CPU local, user programs generally don't know which CPU
// they're running on. (Though we're not multiprocessing yet anyway.)
void start() {
	__default_section_init();
	// FIXME It's really a bug that we end up using loads of stack here. Maybe
	// ph_mergepairs has to be made norecursive, or the problem is that a bug
	// leads it into an infinite loop?
	__more_stack(0xff000);
	printf("apic: starting...\n");

	// Perhaps we should use ACPI information to tell us if/that there's an
	// APIC and where we can find it.
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE,
		(void*)apic, apic_pbase, sizeof(apic));
	// FIXME Should map uncacheable!

	apic[TIMER_DIV] = TIMER_DIV_128;
	apic[APICTIC] = 0;
	apic[TIMER_LVT] = TIMER_TMM_ONESHOT | apic_timer_irq;
	apic[PERFC_LVT] = LVT_MASK;
	// These are bogus - we should consult ACPI to ask what the trigger mode
	// etc of these two are. In principle they could be anything.
	apic[LINT0_LVT] = LVT_TGM_EDGE | LVT_MT_EXT;
	apic[LINT1_LVT] = LVT_MT_NMI;
	apic[ERROR_LVT] = LVT_MASK;

	// Register irq 48 with irq driver
	// Note we don't use the PIC driver here - APIC interrupts have their own
	// EOI etc.
	uintptr_t arg = apic_timer_irq;
	sendrcv1(MSG_REG_IRQ, irq_driver, &arg);

	printf("apic: irq registered, enabling APIC and EOI:ing\n");

	// enable and set spurious interrupt vector to 0xff
	apic[SPURIOUS] |= APIC_SOFTWARE_ENABLE | 0xff;

	setTIC(-1);

	bool need_eoi = true;
	for (;;) {
		{
			u64 tick_counter = get_tick_counter();
			//send1(MSG_IRQ_ACK, irq_driver, arg1);
			printf("T: %lu %lums\n", tick_counter, static_data.ms_counter);
			if (!timers_head) {
				printf("apic: idle.\n");
			} else if (timers_head->timeout <= tick_counter) {
				printf("apic: triggered %p.\n", timers_head);
				timer* t = timer_pop(&timers_head);
				pulse((uintptr_t)t, 1 << t->pulse);
				hmod_delete((uintptr_t)t);
				timer_free(t);
			}
			if (timers_head) {
				u64 t_next = timers_head->timeout - tick_counter;
				printf("apic: time to next timeout %lu ticks\n", t_next);
				setTIC(t_next);
			}
			if (need_eoi) {
				apic[EOI] = 0;
				printf("apic: EOI\n");
				need_eoi = false;
			}
		}

		uintptr_t rcpt = fresh_handle;
		uintptr_t arg1, arg2;
		printf("apic: receiving\n");
		const uintptr_t msg = recv2(&rcpt, &arg1, &arg2);

		printf("apic: received %x from %p: %lx %lx\n", msg&0xff, rcpt, arg1, arg2);

		if (rcpt == irq_driver && (msg & 0xff) == MSG_IRQ_T) {
			printf("apic: irq\n");
			// Keep the counter counting please. (Switch back to periodic mode?)
			setTIC((u32)-1);
			need_eoi = true;
			continue;
		}

		printf("apic: received %x from %p: %lx %lx\n", msg&0xff, rcpt, arg1, arg2);
		switch (msg & 0xff) {
		case MSG_REG_TIMER:
			// FIXME Check if the rcpt is already registered as a timer.
			hmod_rename(rcpt, (uintptr_t)reg_timer(arg1, arg2));
			break;
		case MSG_PFAULT:
			*(volatile u64*)&static_data;
			grant(rcpt, &static_data, PROT_READ);
			break;
		}
	}
}
