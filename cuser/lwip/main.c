#include "lwip/autoip.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/ip.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"

#include <assert.h>
#include <stdbool.h>

#include "http.h"

#include "common.h"
#include "msg_ethernet.h"
#include "msg_timer.h"

#define log printf
#if 0
#define debug log
#else
#define debug(...) (void)0
#endif

#define HEXDUMP 0

static const uintptr_t eth_handle = 7;
static const uintptr_t apic_handle = 4;
static const uintptr_t proto_handle = 0x100;
static const uintptr_t timer_handle = 0x101;
static const uintptr_t fresh_handle = 0x200;

// static const u16 ETHERTYPE_ARP = 0x0806;

#define NBUFS 32
#define BUFFER_SIZE 4096
static char receive_buffers[NBUFS][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
static char send_buffers[NBUFS][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
static bool send_busy[NBUFS];

extern void netif_rcvd(const char*, size_t);

struct netif netif;
static ip_addr_t ipaddr, netmask, gw;
static u64 hwaddr;

// Should be a struct somewhere we can share it with the apic implementation.
static volatile const struct {
	u64 tick_counter;
	u64 ms_counter;
} timer_data PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);

#if 1
u32 sys_now() {
	u64 ms = 0, ticks = 0;
	sendrcv2(MSG_TIMER_GETTIME, apic_handle, &ms, &ticks);
	debug("sys_now: %lu ms (%lu ticks)\n", ms, ticks);
	return ms;
}
#else
u32 sys_now() {
	u64 res = timer_data.ms_counter;
	debug("sys_now: %lu ms (%lu ticks)\n", res, timer_data.tick_counter);
	return res;
}
#endif

// TODO Actually implement a random number generator and an API
u32 lwip_random() {
	return (u32)timer_data.tick_counter;
}

static void check_timers(void) {
	sys_check_timeouts();
	u64 timeout_ms = sys_timeouts_sleeptime();
	if (timeout_ms != (u32)-1) {
		debug("lwip: timeout %lums\n", timeout_ms);
		hmod(apic_handle, apic_handle, timer_handle);
		send2(MSG_REG_TIMER, timer_handle, timeout_ms * 1000000, 0);
	}
}

static void rcvd(uintptr_t buffer_index, uintptr_t packet_length) {
	debug("lwip: received %u bytes\n", packet_length);
#if HEXDUMP
	hexdump(receive_buffers[buffer_index], packet_length);
#endif
	struct pbuf* p = pbuf_alloc(PBUF_LINK, packet_length, PBUF_POOL);
	const char* src = receive_buffers[buffer_index];
	pbuf_take(p, src, packet_length);
	netif.input(p, &netif);
	// Prepare to receive the next message
	send1(MSG_ETHERNET_RECV, proto_handle, buffer_index);
}

static void buffer_done(uintptr_t i) {
	if (i < NBUFS) {
		debug("lwip: finish recv on %d\n", i);
		rcvd(i, *(u16*)(receive_buffers[i] + 4094));
	} else {
		debug("lwip: finish send on %d\n", i);
		send_busy[i - NBUFS] = false;
	}
}

static err_t if_output(struct netif* netif, struct pbuf* p) {
	for (size_t i = 0; i < NBUFS; i++) {
		if (send_busy[i]) {
			continue;
		}
		size_t len = pbuf_copy_partial(p, send_buffers[i], BUFFER_SIZE, 0);
		debug("if_output: %ld bytes\n", len);
#if HEXDUMP
		hexdump(send_buffers[i], len);
#endif
		send_busy[i] = true;
		send2(MSG_ETHERNET_SEND, proto_handle, NBUFS + i, len);
		return 0;
	}

	log("if_output: busy...\n");
	return ERR_WOULDBLOCK;
}

static err_t if_init(struct netif* netif) {
	netif->hwaddr_len = 6;
	netif->name[0] = 'e'; netif->name[1] = 'n';
	netif->output = etharp_output;
	netif->linkoutput = if_output;
	netif->mtu = 1500;
	memcpy(netif->hwaddr, &hwaddr, 6);
	netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_ETHERNET;
	return 0;
}

void start() {
	__default_section_init();

	map(apic_handle, PROT_READ, &timer_data, 0, 4096);
	prefault(&timer_data, PROT_READ);
	debug("lwip: initialized timer\n");

	hmod(eth_handle, eth_handle, proto_handle);
	ipc_arg_t arg1 = ETHERTYPE_ANY;
	sendrcv1(MSG_ETHERNET_REG_PROTO, proto_handle, &arg1);
	hwaddr = arg1;
	debug("lwip: registered protocol on %012lx, mapping+faulting buffer...\n", hwaddr);

	map(proto_handle, PROT_READ, receive_buffers, 0, sizeof(receive_buffers));
	map(proto_handle, PROT_READ | PROT_WRITE, send_buffers, sizeof(receive_buffers), sizeof(send_buffers));
	for (int i = 0; i < NBUFS; i++) {
		prefault(receive_buffers[i], PROT_READ);
		send1(MSG_ETHERNET_RECV, proto_handle, i);
		prefault(send_buffers[i], PROT_READ | PROT_WRITE);
	}
	debug("lwip: registered protocol\n");
	puts("lwip: starting lwIP " LWIP_VERSION_STRING "...\n");

	lwip_init();
	IP4_ADDR(&ipaddr, 192,168,100,3);
	IP4_ADDR(&netmask, 255,255,255,0);
	IP4_ADDR(&gw, 192,168,100,1);
	netif_add(&netif, &ipaddr, &netmask, &gw, NULL, if_init, ethernet_input);
	// Set hardware address of netif. The ethernet driver doesn't send it
	// though, it doesn't even know its own MAC yet.
	netif_set_default(&netif);
	netif_set_up(&netif);
	// If IPv6: set linklocal address for interface
#if LWIP_AUTOIP
	autoip_start(&netif);
#elif LWIP_DHCP
	dhcp_start(&netif);
#endif

	http_start();

	check_timers();
	for (;;) {
		ipc_dest_t rcpt = fresh_handle;
		ipc_arg_t arg2 = 0;
		ipc_msg_t msg = recv2(&rcpt, &arg1, &arg2);
		//debug("lwip: received %lx from %lx: %lx %lx\n", msg, rcpt, arg1, arg2);
		if (rcpt == proto_handle) {
			switch (msg & 0xff) {
			case MSG_PULSE:
				for (int i = 0; i < 2 * NBUFS; i++) {
					if (arg1 & (UINT64_C(1) << i)) {
						buffer_done(i);
					}
				}
				break;
			default:
				debug("lwip: received %lx from %lx: %lx %lx\n", msg, rcpt, arg1, arg2);
				assert(false);
				break;
			}
		} else if (rcpt == timer_handle) {
			//debug("lwip: timer\n");
		} else {
			debug("lwip: received %lx from %lx: %lx %lx\n", msg, rcpt, arg1, arg2);
		}
		if (rcpt == fresh_handle) {
			hmod_delete(rcpt);
		}
		check_timers();
	}
}
