#include "common.h"

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

static volatile u32 apic[1024] PLACEHOLDER_SECTION ALIGN(4096);
// TODO Make a page of timer information that can be shared.
static uintptr_t tick_counter;

typedef struct timer timer;
struct timer {
	u64 timeout;
	timer* down;
	timer* right;
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

static timer* timers_head;
// one MILLION timers
#define MAX_TIMERS (1048576)
static timer timer_heap[MAX_TIMERS];
static u32 timer_heap_limit = 0;
static timer* free_timers;
static u32 prevTIC = 0;

#define MAX_U32(x) ((x) > (u32)-1 ? (u32)-1 : (x))
void setTIC(u64 ticks) {
	ticks = MAX_U32(ticks);
	prevTIC = ticks;
	apic[APICTIC] = ticks;
}
u32 getElapsedTCC() {
	return prevTIC - apic[APICTCC];
}

timer* timer_alloc() {
	if (free_timers) {
		return timer_pop(&free_timers);
	} else {
		return &timer_heap[timer_heap_limit++];
	}
}
timer* timer_new(u64 timeout) {
	timer* t = timer_alloc();
	t->timeout = timeout;
	return t;
}
void timer_free(timer* t) {
	t->timeout = (uintptr_t)t;
	timer_add(&free_timers, t);
}
timer* reg_timer(u64 ns) {
	u64 ticks = (ns / 1000) * apic_ticks / 1000000;
	printf("apic: %lu ns -> %lu ticks\n", ns, ticks);
	u64 old_timeout = timers_head ? timers_head->timeout : (u64)-1;
	u64 tick_timeout = tick_counter + ticks;
	printf("apic: old timeout in %lu ticks\n", old_timeout - tick_counter);
	timer* t = timer_add(&timers_head, timer_new(tick_timeout));
	if (tick_timeout < old_timeout) {
		printf("apic: new deadline %lu ticks\n", tick_timeout - tick_counter);
		setTIC(ticks);
	}
	return t;
}

// Fun stuff: APIC is CPU local, user programs generally don't know which CPU
// they're running on. (Though we're not multiprocessing yet anyway.)
void start() {
	__default_section_init();
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
	uintptr_t arg = apic_timer_irq;
	sendrcv1(MSG_REG_IRQ, irq_driver, &arg);

	printf("apic: irq registered, enabling APIC and EOI:ing\n");

	// enable and set spurious interrupt vector to 0xff
	apic[SPURIOUS] |= APIC_SOFTWARE_ENABLE | 0xff;

	// Set end-of-interrupt flag so we get some interrupts.
	apic[EOI] = 0;
	// Set the task priority register to 0 (accept all interrupts)
	// Set this in the kernel somewheres...
	//zero	eax
	//mov	cr8,rax

	for (;;) {
		uintptr_t rcpt = fresh_handle;
		uintptr_t arg1, arg2;
		const uintptr_t msg = recv2(&rcpt, &arg1, &arg2);

		if (rcpt == irq_driver && (msg & 0xff) == MSG_IRQ_T) {
			//send1(MSG_IRQ_ACK, irq_driver, arg1);
			u32 tcc = getElapsedTCC();
			tick_counter += tcc;
			printf("T: %lu [tcc=%u]\n", tick_counter, tcc);
			if (!timers_head) {
				printf("apic: woken up with no timers? boo.\n");
				setTIC(0);
				apic[EOI] = 0;
				continue;
			}
			if (timers_head->timeout <= tick_counter) {
				printf("apic: triggered %p.\n", timers_head);
				timer* t = timer_pop(&timers_head);
				send1(MSG_TIMER_T, (uintptr_t)t, tick_counter);
				hmod_delete((uintptr_t)t);
				timer_free(t);
			}
			if (timers_head) {
				u64 t_next = timers_head->timeout - tick_counter;
				printf("apic: time to next timeout %lu ticks\n", t_next);
				setTIC(t_next);
			}
			apic[EOI] = 0;
			continue;
		}

		printf("apic: received %x from %p: %lx %lx\n", msg&0xff, rcpt, arg1, arg2);
		switch (msg & 0xff) {
		case MSG_REG_TIMER: {
			timer* t = reg_timer(arg1);
			hmod(rcpt, (uintptr_t)t, 0);
			break;
		}
		}
	}
}
