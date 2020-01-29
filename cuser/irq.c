#include <sb1.h>
#include "common.h"
#include "msg_irq.h"

#include <stdbool.h>
#include <stdlib.h>

#define LOG_ENABLED 0
#define log(fmt, ...) do { if (LOG_ENABLED) printf("irq: " fmt, ## __VA_ARGS__); } while (0)

// At most 256 IRQs possible. Keep a bit for each so we can know if it's registered or should be ignored
bool registered[256];

// File descriptor that registered an interest in that IRQ.
int registered_fd[256];

static void deliver_irq(int irq) {
    // The kernel-side IRQ numbering maps IRQ 32 to 0, since the first 32
    // interrupt vectors are exceptions.
    irq += 32;
    if (registered[irq]) {
        log("IRQ %d registered to %d, delivering\n", irq, registered_fd[irq]);
        pulse(registered_fd[irq], 1);
    }
    else {
        log("IRQ %d unregistered\n", irq);
    }
}

static void handle_irqs(uint64_t arg) {
    int irq = 0;
    while (arg) {
        if (arg & 1) {
            deliver_irq(irq);
        }
        arg >>= 1;
        irq++;
    }
}

static void reg_irq(int irq, ipc_dest_t rcpt) {
    log("Registering IRQ %d to %d (acceptfd = %d)\n", irq, msg_dest_fd(rcpt), !!(rcpt & MSG_TX_ACCEPTFD));

    // Currently no support for multiple recipients of raw IRQs. The assumption
    // is that ranges of IRQs are uniquely mapped to some interrupt controller
    // and that's the thing that registers with us.
    if (registered[irq]) {
        log("IRQ %d already registered (to %d)\n", irq, registered_fd[irq]);
        send1(MSG_REG_IRQ, rcpt, -1);
        return;
    }

    registered[irq] = true;
    if (rcpt & MSG_TX_ACCEPTFD) {
        // create a new socket pair for the IRQ, register and return that
        int fds[2];
        socketpair(fds);

        registered_fd[irq] = fds[0];
        log("responding with local fd %d, remote fd %d...\n", fds[0], fds[1]);
        send1(MSG_REG_IRQ, rcpt | MSG_TX_CLOSEFD, fds[1]);
    } else {
        registered_fd[irq] = msg_dest_fd(rcpt);
        log("responding with original fd %d...\n", msg_dest_fd(rcpt));
        send1(MSG_REG_IRQ, rcpt, 0);
    }

    log("Registered IRQ %d to %d (done)\n", irq, registered_fd[irq]);
}

void start() {
    __default_section_init();
    log("started.\n");

    for (;;) {
        ipc_arg_t arg1, arg2, arg3;
        ipc_dest_t rcpt = msg_set_fd(0, -1);;
        log("receiving...\n");
        ipc_msg_t msg = recv3(&rcpt, &arg1, &arg2, &arg3);
        if (msg < 0) {
            log("Got error %ld from recv\n", msg);
            abort();
        }
        log("Received %lx from %lx args %lx %lx %lx\n", msg, rcpt, arg1, arg2, arg3);
        switch (msg & 0xff)
        {
        case SYS_PULSE:
            if (msg_dest_fd(rcpt) < 0) {
                handle_irqs(arg1);
            }
            break;
        case MSG_IRQ_ACK:
            // Ignored
            break;
        case MSG_REG_IRQ:
            // arg1 << 8 contains flags such as edge trigger and polarity, but
            // those are not relevant for the rawIRQ driver.
            reg_irq(arg1 & 0xff, rcpt);
            break;
        default:
            log("Unknown message %#lx from %#lx\n", msg & 0xff, rcpt);
        }
    }
}
