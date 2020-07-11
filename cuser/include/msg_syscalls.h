#ifndef __MSG_SYSCALLS_H
#define __MSG_SYSCALLS_H

#include <stdint.h>

#if __cplusplus >= 201109L
#define CONSTEXPR constexpr
#else
#define CONSTEXPR
#endif

// IPC destination: the low 32-bits are a file descriptor, the high 32 bits
// contain the transaction ID and flags. Transaction IDs should be considered
// opaque, they're assigned by the kernel.
// Transaction IDs are only assigned and used for call/sendrcv IPCs.
typedef uint64_t ipc_dest_t;

enum msg_tx_flags {
	// Sign bit indicates error return from receive, not a transaction.
	MSG_TX_ERROR = 1ULL << 63,
	// First response word must be a file descriptor to send.
	MSG_TX_ACCEPTFD = 1ULL << 62,
	// A file descriptor is returned to a MSG_TX_ACCEPTFD call (alias for MSG_TX_ACCEPTFD).
	MSG_TX_FD = 1ULL << 62,
	// Page-fault transaction, response must be sent with grant(). Must not be set in an outgoing message.
	MSG_TX_PFAULT = 1ULL << 61,
	// When sending a response with the MSG_TX_FD flag, close the file
	// descriptor on the sending side as soon as it's been sent to the
	// recipient.
	MSG_TX_CLOSEFD = 1ULL << 60,
};

// Build a destination from fd and flags.
//
// For call syscalls the transaction ID will be filled in by the kernel. For
// responding to a syscall, the whole ipc_dest_t should be saved and used as it
// includes fd, transaction ID and flags.
// MSG_TX_CLOSEFD may be or'ed into the IPC dest on return though.
static inline CONSTEXPR ipc_dest_t msg_mkdest(int32_t fd, enum msg_tx_flags flags) {
	return flags | (uint64_t)fd;
}
// Get the file descriptor field from an IPC destination.
static inline CONSTEXPR int msg_dest_fd(ipc_dest_t dest) {
	return (int32_t)(uint32_t)dest;
}
// Get the tx_id and flags
static inline CONSTEXPR uint64_t msg_txid(ipc_dest_t dest) {
	return (dest >> 32) << 32;
}
static inline CONSTEXPR uint64_t msg_set_fd(ipc_dest_t dest, uint32_t fd) {
	return msg_txid(dest) | fd;
}

// IPC argument: this is the size of a message payload register.
typedef uint64_t ipc_arg_t;
// IPC message (and/or syscall number):
// - recv IPC is just a single syscall (#0)
// - real syscalls: 0 until MSG_USER-1
// - large range for message IDs for IPC: MSG_USER..MSG_USER_LAST
// - call IPC is marked by high bit set (if clear it's a one-way send IPC instead)
// - negative numbers are also returned as errors (unclear if sending errors is
//   allowed, maybe a user range of errors will be provided)
typedef int64_t ipc_msg_t;

typedef int64_t off_t; // -> sys/types.h

enum msg_kind {
	MSG_KIND_SEND = 0,
	MSG_KIND_CALL = 1 << 8,
};

static inline ipc_msg_t msg_send(ipc_msg_t msg) {
	return msg;
}
static inline ipc_msg_t msg_call(ipc_msg_t msg) {
	return msg | (ipc_msg_t)MSG_KIND_CALL;
}
static inline uint8_t msg_code(ipc_msg_t msg) {
	return (enum msg_kind)(msg & ~MSG_KIND_CALL);
}
static inline enum msg_kind msg_get_kind(ipc_msg_t msg) {
	return (enum msg_kind)(msg & MSG_KIND_CALL);
}

enum syscalls_builtins {
	SYS_RECV = 0,
	SYS_MAP = 1,
	SYS_PFAULT = 2,
	SYS_UNMAP = 3,
	// SYS_HMOD = 4, no longer implemented
	SYS_NEWPROC = 5,
	SYS_WRITE = 6,
	// arg0 (dst) = port
	// arg1 = flags (i/o) and data size:
	//  0x10 = write (or with data size)
	//  0x01 = byte
	//  0x02 = word
	//  0x04 = dword
	// arg2 (if applicable) = data for output
	SYS_IO = 7, // Backdoor!
	SYS_GRANT = 8,
	SYS_PULSE = 9,
	SYS_YIELD = 10,
	SYS_SOCKETPAIR = 11,
	SYS_CLOSE = 12,
};
enum {
	// First user-available message number. Indicates a send or call IPC rather than a syscall.
	MSG_USER = 16,
	// Last user-available message number.
	MSG_USER_LAST = MSG_KIND_CALL - 1,
};

#endif /* __MSG_SYSCALLS_H */
