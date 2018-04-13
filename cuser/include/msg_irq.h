#ifndef __MSG_IRQ_H
#define __MSG_IRQ_H

#include "msg_syscalls.h"

enum msg_irq {
	/**
	 * Takes one argument, the IRQ number (GSI in the case of I/O APICs).
	 * (Or does it take a number local to the interrupt controller?)
	 */
	MSG_REG_IRQ = MSG_USER,
	/**
	 * Acknowledge receipt of the interrupt.
	 *
	 * arg1: IRQ number to acknowledge
	 */
	MSG_IRQ_ACK,
};

#endif /* __MSG_IRQ_H */
