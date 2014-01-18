#include "lwip/autoip.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/ip.h"
#include "lwip/timers.h"
#include "netif/etharp.h"

#ifdef NDEBUG
#define debug(...) (void)0
#else
#define debug(...) printf(__VA_ARGS__)
#endif

#include "common.h"

err_t http_start(void);

static const uintptr_t eth_handle = 5;
static const uintptr_t apic_handle = 6;
static const uintptr_t proto_handle = 0x100;
static const uintptr_t timer_handle = 0x101;
static const uintptr_t fresh_handle = 0x200;

static const u16 ETHERTYPE_ARP = 0x0806;

#define NBUFS 1
#define BUFFER_SIZE 4096
static char receive_buffers[NBUFS][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
static char send_buffers[NBUFS][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);
static bool send_busy[NBUFS];

extern void netif_rcvd(const char*, size_t);

struct netif netif;
static ip_addr_t ipaddr, netmask, gw;
static u64 hwaddr;

// Should be a struct somewhere we can share it with the apic implementation.
static struct {
	u64 tick_counter;
	u64 ms_counter;
} timer_data PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);

u32 sys_now() {
	u32 res = timer_data.ms_counter;
	//printf("sys_now: %u\n", res);
	return res;
}
void check_timers() {
	u64 timeout_ms = sys_check_timeouts();
	if (timeout_ms != (u32)-1) {
		hmod(apic_handle, apic_handle, timer_handle);
		send2(MSG_REG_TIMER, timer_handle, timeout_ms * 1000000, 0);
	}
}

static void rcvd(uintptr_t buffer_index, uintptr_t packet_length) {
	debug("lwip: received %u bytes\n", packet_length);
#ifndef NDEBUG
	hexdump(receive_buffers[buffer_index], packet_length);
#endif
	struct pbuf* p = pbuf_alloc(PBUF_LINK, packet_length, PBUF_POOL);
	const char* src = receive_buffers[buffer_index];
	for (struct pbuf* q = p; q != NULL; q = q->next) {
		memcpy(q->payload, src, q->len);
		src += q->len;
	}
	send1(MSG_ETHERNET_RCVD, proto_handle, buffer_index);
	netif.input(p, &netif);
}

static err_t if_output(struct netif* netif, struct pbuf* p) {
	if (send_busy[0]) {
		printf("if_output: busy...\n");
		return EAGAIN;
	}
	size_t len = 0;
	while (p) {
		memcpy(send_buffers[0] + len, p->payload, p->len);
		len += p->len;
		p = p->next;
	}
	debug("if_output: %ld bytes\n", len);
#ifndef NDEBUG
	hexdump(send_buffers[0], len);
#endif
	send_busy[0] = true;
	send2(MSG_ETHERNET_SEND, proto_handle, NBUFS + 0, len);
	return 0;
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

	hmod(eth_handle, eth_handle, proto_handle);
	uintptr_t arg1 = ETHERTYPE_ANY, arg2 = NBUFS;
	sendrcv2(MSG_ETHERNET_REG_PROTO, proto_handle, &arg1, &arg2);
	hwaddr = arg1;
	debug("lwip: registered protocol on %012lx, mapping+faulting buffer...\n", hwaddr);

	map(proto_handle, PROT_READ, receive_buffers, 0, sizeof(receive_buffers));
	map(proto_handle, PROT_READ | PROT_WRITE, send_buffers, sizeof(receive_buffers), sizeof(send_buffers));
	prefault(receive_buffers[0], PROT_READ);
	prefault(send_buffers[0], PROT_READ | PROT_WRITE);
	puts("lwip: registered protocol\n");

	map(apic_handle, PROT_READ, &timer_data, 0, 4096);
	prefault(&timer_data, PROT_READ);
	puts("lwip: initialized timer\n");

	lwip_init();
	IP4_ADDR(&ipaddr, 192,168,100,3);
	IP4_ADDR(&netmask, 255,255,255,0);
#if !(LWIP_AUTOIP || LWIP_DHCP)
	IP4_ADDR(&gw, 192,168,100,1);
#else
	IP4_ADDR(&gw, 0,0,0,0);
#endif
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
		uintptr_t rcpt = fresh_handle;
		uintptr_t msg = recv2(&rcpt, &arg1, &arg2);
		if (rcpt == proto_handle) {
			switch (msg) {
			case MSG_ETHERNET_RCVD:
				debug("lwip: received %ld bytes in buf %ld\n", arg2, arg1);
				rcvd(arg1, arg2);
				break;
			case MSG_ETHERNET_SEND:
				debug("lwip: send %ld acked\n", arg1);
				arg1 -= NBUFS;
				if (arg1 < NBUFS) {
					send_busy[arg1] = false;
				}
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
