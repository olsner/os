#define NO_SYS 1

// options, options, options!
#define MEMP_OVERFLOW_CHECK 1
#define MEMP_SANITY_CHECK 1
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

#ifdef NDEBUG
#define LWIP_DBG_NDEBUG 0
#else
#define LWIP_DBG_NDEBUG LWIP_DBG_ON
#endif
#define LWIP_DBG_TYPES_ON (LWIP_DBG_ON | LWIP_DBG_STATE)

#define LWIP_DEBUG
#define ETHARP_DEBUG LWIP_DBG_NDEBUG
#define AUTOIP_DEBUG LWIP_DBG_NDEBUG
#define DHCP_DEBUG   LWIP_DBG_ON
#define IP_DEBUG LWIP_DBG_NDEBUG
#define INET_DEBUG LWIP_DBG_NDEBUG
#define ICMP_DEBUG LWIP_DBG_NDEBUG
#define TCP_DEBUG LWIP_DBG_NDEBUG
#define TCP_INPUT_DEBUG LWIP_DBG_NDEBUG
#define TCP_OUTPUT_DEBUG LWIP_DBG_NDEBUG
