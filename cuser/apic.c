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

//REG(TPR, 0x80);
REG(EOI, 0xb0);
REG(LDR, 0xd0);
REG(DFR, 0xe0);
REG(SPURIOUS, 0xf0);
enum SpuriousReg {
	APIC_SOFTWARE_ENABLE = 0x100,
};
REG(ICR_LOW, 0x300);
REG(ICR_HIGH, 0x310);
enum ICRReg {
	ICR_DELIVERY_STATUS_PENDING = 1 << 12,
};
enum ICRDeliveryMode {
	// fixed, lowest-priority, SMI, Reserved, NMI
	ICR_DELIVERY_FIXED = 0,
	ICR_DELIVERY_INIT = 5,
	ICR_DELIVERY_SIPI = 6,
	// 7 = Reserved
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
	u64 padding[509];
} static_data ALIGN(4096);

static u64 ticks_to_millis(u64 ticks) {
	return ticks / (apic_ticks / 1000);
}
static u64 ticks_to_nanos(u64 ticks) {
	return 500 * ticks / (apic_ticks / 2000000);
}

u64 get_tick_counter(void) {
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

timer* timer_alloc(void) {
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

static void wait_for_ipi_delivery(bool wanted_status) {
	uint64_t n = 0;
	const u32 mask = ICR_DELIVERY_STATUS_PENDING;
	u32 wanted = wanted_status ? mask : 0;
	while ((apic[ICR_LOW] & mask) != wanted) {
		n++;
	}
	if (n) {
		printf("Waited %lu times for previous IPI.", n);
	}
}

void send_ipi(u8 dest_apic, u8 ipi_type, u8 vector) {
	printf("apic: sending %d IPI to %#x (vector %#x)\n", ipi_type, dest_apic, vector);
	printf("apic: current ICR_LOW: %#x\n", apic[ICR_LOW]);

	if (apic[ICR_LOW] & ICR_DELIVERY_STATUS_PENDING) {
		printf("Previous IPI is pending, waiting...\n");
		wait_for_ipi_delivery(false);
	}

	// Some of these might be "dangerous" (actually probably all of them are
	// - exception handlers probably don't work right when delivered as an IPI).
	// Note that INIT de-assert is not supported. This should only affect old
	// CPUs we don't support anyway.
	assert(ipi_type == ICR_DELIVERY_FIXED
			|| ipi_type == ICR_DELIVERY_INIT
			|| ipi_type == ICR_DELIVERY_SIPI);

	const u32 high = dest_apic << 24;
	const u32 low =
		vector
		| (ipi_type << 8)
		// bit 11: destination_mode = 0
		// bit 12: delivery status = 0
		// bit 13: reserved
		// bit 14: level = 1 (asserted)
		| (1 << 14)
		// bit 15: trigger = 0 (edge)
		// bit 16/17: reserved
		// bit 18..19: destination_shorthand = 0
		;

	printf("Sending IPI %#08x%08x\n", high, low);

	apic[ICR_HIGH] = high;
	apic[ICR_LOW] = low;

	printf("Sent IPI %#08x%08x\n", apic[ICR_HIGH], apic[ICR_LOW]);

	if (apic[ICR_LOW] & ICR_DELIVERY_STATUS_PENDING) {
		printf("IPI delivery is pending, waiting...\n");
		wait_for_ipi_delivery(false);
	}
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
	map(0, MAP_PHYS | PROT_READ | PROT_WRITE | PROT_NO_CACHE,
		(void*)apic, apic_pbase, sizeof(apic));

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

	// Set the mask to listen on. Should depend on CPU number.
	apic[DFR] = 1 << 24;
	// 0xf = flat model rather than cluster model
	// rest of the bits should be all 1s
	apic[LDR] = 0xffffffff;

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

		// Note that since we use the raw IRQ driver, we don't need to ACK the
		// IRQ's we get - it's not listening for that anyway.
		if (rcpt == irq_driver) {
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
		case MSG_TIMER_GETTIME:
			if (msg_get_kind(msg) == MSG_KIND_CALL) {
				u64 ticks = get_tick_counter();
				send2(msg & 0xff, rcpt, static_data.ms_counter, ticks);
			} else {
				printf("apic: gettime must be a sendrcv call\n");
			}
			if (rcpt == fresh_handle) {
				hmod_delete(rcpt);
			}
			break;
		case MSG_PFAULT:
			*(volatile u64*)&static_data;
			grant(rcpt, &static_data, PROT_READ);
			break;
		case MSG_APIC_SEND_IPI:
			send_ipi(arg1, arg1 >> 8, arg1 >> 16);
			break;
		}
	}
}
