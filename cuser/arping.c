#include "common.h"

static const uintptr_t eth_handle = 5;
static const uintptr_t proto_handle = 0x100;

static const u16 ETHERTYPE_ARP = 0x0806;

#define NBUFS 1
#define BUFFER_SIZE 4096
static char receive_buffers[NBUFS][BUFFER_SIZE] PLACEHOLDER_SECTION ALIGN(BUFFER_SIZE);

static void rcvd(uintptr_t buffer_index, uintptr_t packet_length) {
	printf("arping: received %u bytes\n", packet_length);
	hexdump(receive_buffers[buffer_index], packet_length);
}

void start() {
	hmod(eth_handle, eth_handle, proto_handle);
	send2(MSG_ETHERNET_REG_PROTO, proto_handle, ETHERTYPE_ARP, 1);
	printf("arping: registered protocol, mapping+faulting buffer...\n");
	map(proto_handle, PROT_READ, receive_buffers[0], 0, sizeof(receive_buffers[0]));
	prefault(receive_buffers[0], PROT_READ);
	printf("arping: registered protocol\n");
	for (;;) {
		uintptr_t arg1, arg2;
		uintptr_t rcpt = proto_handle;
		uintptr_t msg = recv2(&rcpt, &arg1, &arg2);
		printf("arping: received %x from %x: %x %x\n", msg, rcpt, arg1, arg2);
		switch (msg) {
		case MSG_ETHERNET_RCVD:
			rcvd(arg1, arg2);
			send1(MSG_ETHERNET_RCVD, proto_handle, arg1);
			break;
		}
	}
}
