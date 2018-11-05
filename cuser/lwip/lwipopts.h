#include <stdint.h>

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0

// options, options, options!
#define LWIP_DHCP 1
#define LWIP_AUTOIP 0
#define LWIP_DNS 1
#define LWIP_NETIF_LOOPBACK_MULTITHREADING 0
#define LWIP_HAVE_LOOPIF 1
//#define LWIP_IPV6 1
#define LWIP_PROVIDE_ERRNO
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_TCP 1
#define TCP_LISTEN_BACKLOG 1

uint32_t lwip_random(void);
#define LWIP_RAND lwip_random

#ifdef NDEBUG
// Kind of good to have asserts, but they're like 20% of the IP stack?
#define LWIP_NOASSERT
#endif
#ifndef NDEBUG
#define LWIP_DEBUG
#endif

#ifdef NDEBUG
#define LWIP_DBG_NDEBUG 0
#else
#define LWIP_DBG_NDEBUG LWIP_DBG_ON
#endif
#define LWIP_DBG_TYPES_ON (LWIP_DBG_ON | LWIP_DBG_STATE)

#define ETHARP_DEBUG LWIP_DBG_NDEBUG
#define AUTOIP_DEBUG LWIP_DBG_NDEBUG
#define DHCP_DEBUG LWIP_DBG_NDEBUG
#define IP_DEBUG LWIP_DBG_NDEBUG
#define INET_DEBUG LWIP_DBG_NDEBUG
#define ICMP_DEBUG LWIP_DBG_NDEBUG
#define TCP_DEBUG LWIP_DBG_NDEBUG
#define TCP_INPUT_DEBUG LWIP_DBG_NDEBUG
#define TCP_OUTPUT_DEBUG LWIP_DBG_NDEBUG

#define TIMERS_DEBUG LWIP_DBG_NDEBUG
#define LWIP_DEBUG_TIMERNAMES (TIMERS_DEBUG & LWIP_DBG_ON)

// Disable to get rid of a timer.
#define IP_REASSEMBLY 0
// Also disabled to get rid of timers
#define LWIP_ACD 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
