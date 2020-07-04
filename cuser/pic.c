#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <sb1.h>
#include "common.h"
#include "msg_irq.h"

#define LOG_ENABLED 0
#define log(fmt, ...) do { if (LOG_ENABLED) printf("pic: " fmt, ## __VA_ARGS__); } while (0)

#define PIC1_CMD	0x20
#define PIC1_DATA	0x21
#define PIC2_CMD	0xa0
#define PIC2_DATA	0xa1

#define ICW1_ICW4	0x01
#define ICW1_INIT	0x10

// 4 = bit 2 set = there's a slave on 2
#define ICW3_MASTER	4
// 2 = slave has slave ID 2
#define ICW3_SLAVE	2

#define ICW4_8086	0x01

// Send this on the command port to signal EOI
#define PIC_EOI		0x20
// Send this + IRQ to do a specific EOI for some interrupt
#define PIC_SEOI	0x60

#define NUM_IRQS 16

// Offset to where the PIC are mapped by start32 (0x20..0x2f)
#define PIC_IRQ_OFFSET 0x20

// rawIRQ handles
int upstream_irq[NUM_IRQS];
// handles for downstream processes (usually all connected to ACPICA since that
// handles IRQ routing and selecting between PIC and IOAPIC).
int downstream_fds[NUM_IRQS];

static const int irq_driver = 1;

static void pic_unmask(int port, int pin) {
    u8 mask = inb(port);
    mask &= ~(1 << pin);
    outb(port, mask);
}
static void pic_mask(int port, int pin) {
    u8 mask = inb(port);
    mask |= 1 << pin;
    outb(port, mask);
}

static void unmask(int irq) {
    log("Unmasking %d\n", irq);

    if (irq >= 8) {
        pic_unmask(PIC2_DATA, irq - 8);
        pic_unmask(PIC1_DATA, 2);
    }
    else {
        pic_unmask(PIC1_DATA, irq);
    }
}

static void reg_irq(int irq, uintptr_t rcpt) {
    log("Registering IRQ %d to %lx/%d\n", irq, rcpt >> 32, msg_dest_fd(rcpt));
    assert(irq < NUM_IRQS);
    assert(downstream_fds[irq] < 0);
    assert(rcpt & MSG_TX_ACCEPTFD);

    int fds[2];
    socketpair(fds);
    downstream_fds[irq] = fds[0];
    send1(MSG_REG_IRQ, MSG_TX_CLOSEFD | rcpt, fds[1]);
    log("IRQ %d registered to %d\n", irq, downstream_fds[irq]);

    unmask(irq);
}

static void handle_irq(int irq) {
    log("IRQ %d triggered\n", irq);

    if (irq >= 8) {
        // Slave IRQ. Mask the slave irq and EOI the slave PIC.
        // We don't mask the master's slave IRQ since there may be more slave
        // IRQs of relevance.
        pic_mask(PIC2_DATA, irq - 8);
        outb(PIC2_CMD, PIC_EOI);
    }
    else {
        // Master IRQ. Mask the trigger interrupt.
        pic_mask(PIC2_DATA, irq);
    }

    // Always EOI the master PIC - if it's a slave IRQ then IRQ 2 will have
    // also triggered and needs to be EOId.
    // (Actually, the master PIC knows this is a slave interrupt from the
    // configuration so I'm not too sure it wants an EOI for it.)
    outb(PIC1_CMD, PIC_EOI);

    assert(downstream_fds[irq] >= 0);
    pulse(downstream_fds[irq], 1);
}

static void ack_irq(int irq) {
    log("IRQ %d acknowledged\n", irq);
    unmask(irq);
}

static int find_fd(const int fd, const int* array, int n) {
    for (int i = 0; i < n; i++) {
        if (array[i] == fd) {
            return i;
        }
    }
    return -1;
}
static int get_irq_from_upstream(int fd) {
    return find_fd(fd, upstream_irq, NUM_IRQS);
}
static int get_irq_from_downstream(int fd) {
    return find_fd(fd, downstream_fds, NUM_IRQS);
}

void start() {
    __default_section_init();
    log("booting...\n");

    memset(downstream_fds, 0xff, sizeof(downstream_fds));

    for (int i = 0; i < NUM_IRQS; i++) {
        ipc_arg_t arg = PIC_IRQ_OFFSET + i;
        sendrcv1(MSG_REG_IRQ, MSG_TX_ACCEPTFD | irq_driver, &arg);
        log("rawIRQ %d registered to fd %d\n", i, arg);
        upstream_irq[i] = arg;
    }

    // Reinitialize PIC?
    // * Mask all interrupts (we don't want them until someone registers)
    // * Map either to a constant range of real interrupts, or have some
    // way to "allocate" through the IRQ driver?
    // For now - assume PICs are mapped to 0x20..0x2f and all interrupts are
    // masked (this is what start32.inc does).

    outb(PIC1_CMD, PIC_EOI);
    outb(PIC2_CMD, PIC_EOI);

    log("boot complete\n");

    for (;;) {
        ipc_arg_t arg1, arg2, arg3;
        ipc_dest_t rcpt = msg_set_fd(0, -1);;
        log("receiving...\n");
        ipc_msg_t msg = recv3(&rcpt, &arg1, &arg2, &arg3);
        if (msg < 0) {
            log("Got error %ld from recv\n", msg);
            abort();
        }
        const int fd = msg_dest_fd(rcpt);
        log("Received %lx from %lx/%d args %lx %lx %lx\n", msg, rcpt >> 32, fd, arg1, arg2, arg3);
        switch (msg & 0xff)
        {
        case SYS_PULSE:
            handle_irq(get_irq_from_upstream(fd));
            break;
        case MSG_IRQ_ACK:
            ack_irq(get_irq_from_downstream(fd));
            break;
        case MSG_REG_IRQ:
            // arg1 << 8 contains flags such as edge trigger and polarity, but
            // those are not relevant for the PIC. (or are they?)
            reg_irq(arg1 & 0xff, rcpt);
            break;
        default:
            log("Unknown message %#lx from %#lx\n", msg & 0xff, rcpt);
        }
    }
}
