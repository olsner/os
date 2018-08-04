#ifndef __MSG_SYSCALLS_H
#define __MSG_SYSCALLS_H

enum syscalls_builtins {
	MSG_NONE = 0,
	SYSCALL_RECV = MSG_NONE,
	MSG_MAP,
	MSG_PFAULT,
	MSG_UNMAP,
	MSG_HMOD,
	SYSCALL_WRITE = 6,
	// arg0 (dst) = port
	// arg1 = flags (i/o) and data size:
	//  0x10 = write (or with data size)
	//  0x01 = byte
	//  0x02 = word
	//  0x04 = dword
	// arg2 (if applicable) = data for output
	SYSCALL_IO = 7, // Backdoor!
	MSG_GRANT = 8,
	MSG_PULSE = 9,
	SYSCALL_YIELD = 10,
	MSG_USER = 16,
};

#endif /* __MSG_SYSCALLS_H */
