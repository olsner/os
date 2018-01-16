#ifndef __MSG_TIMER_H
#define __MSG_TIMER_H

enum msg_timer
{
	/**
	 * Register a timer to trigger in approximately N nanoseconds.
	 *
	 * arg1: timeout.
	 */
	MSG_REG_TIMER = MSG_USER,
	/**
	 * A registered timer has triggered. The timer will not be triggered again
	 * unless you re-register a new timeout.
	 */
	MSG_TIMER_T,
	/**
	 * Two-argument sendrcv.
	 *
	 * returns:
	 * arg1: milliseconds
	 * arg2: ticks
	 */
	MSG_TIMER_GETTIME,
};

#endif /* __MSG_TIMER_H */
