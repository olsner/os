#ifndef __MSG_ETHERNET_H
#define __MSG_ETHERNET_H

enum msg_ethernet {
	/**
	 * Register an ethernet protocol. Use with MSG_TX_ACCEPTFD, then memory map
	 * pages on the returned file to read incoming packets and store outgoing
	 * packets.
	 * The special protocol number 0 can be used to match all protocols.
	 *
	 * The number of buffers to use is decided by the protocol through sending
	 * receive messages for each buffer it wants to use for reception. All
	 * buffers start out owned by the protocol until given to ethernet for
	 * sending or receiving.
	 *
	 * arg1: protocol number / ethertype
	 * Returns:
	 * arg1: created protocol file descriptor
	 * arg2: MAC address of card
	 */
	MSG_ETHERNET_REG_PROTO = MSG_USER,
	/**
	 * protocol -> ethernet: allocate a buffer to reception, handing over
	 * ownership to the ethernet driver. Can only be sent without sendrcv. The
	 * reply comes by pulse afterwards.
	 *
	 * The buffer contains the whole ethernet frame including headers, as it
	 * came from the network. It may contain any type of frame, with or without
	 * a VLAN header.
	 *
	 * arg1: page number of the buffer to receive into
	 */
	MSG_ETHERNET_RECV,
	/**
	 * protocol -> ethernet: Send a packet. The given page number is owned by
	 * the ethernet driver until delivered over the wire and the
	 * pulse reply is sent.
	 *
	 * arg1: page number of buffer that contains data to send.
	 * arg2: datagram length
	 * Returns:
	 * arg1: page number of delivered packet - the page is no longer owned by
	 * the driver.
	 */
	MSG_ETHERNET_SEND,
};
enum
{
	ETHERTYPE_ANY = 0,
};

#endif /* __MSG_ETHERNET_H */
