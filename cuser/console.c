#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <sb1.h>
#include "common.h"
#include "msg_irq.h"
#include "msg_con.h"

#include "keymap.h"

#define LOG_ENABLED 0
#define log(fmt, ...) do { if (LOG_ENABLED) printf("console: " fmt, ## __VA_ARGS__); } while (0)

// Set to 1 to disable AT/XT translation and activate scan code set 2.
// The XT codes are easier to deal with though :)
static const bool use_set2 = false;

static const int IRQ_KEYBOARD = 1;
static const u16 KEY_DATA = 0x60;
static const u16 KEY_CMD = 0x64;

static const uintptr_t acpica_handle = 6;

static void clear_8042_buffer(void) {
    do {
        inb(KEY_DATA);
    } while (inb(KEY_CMD) & 1);
}

static void wait_ready_for_write(void) {
    while (inb(KEY_CMD) & 2) {
        // Loop
    }
}

static void setup_8042(void) {
    // bits:
    // 0 = first port IRQ (1 = enabled)
    // 1 = second port irq (disabled)
    // 2 = passed POST (not sure if need to write, but let's say we did pass
    // POST)
    // 3 = should be zero
    // 4 = first ps/2 port clock (1 = disabled)
    // 5 = second ps/2 port clock (1 = disabled)
    // 6 = first ps/2 port translation (1 = enabled)
    //     we want this disabled, to get the raw scan codes from keyboard
    static u8 config_byte = (1 << 0) | (1 << 2);

    if (use_set2) {
        outb(KEY_CMD, 0x60);
        wait_ready_for_write();
        outb(KEY_DATA, config_byte);
        wait_ready_for_write();
        outb(KEY_DATA, 0xff); // Reset command
        wait_ready_for_write();
        outb(KEY_DATA, 0xf0); // Set scan code set
        wait_ready_for_write();
        outb(KEY_DATA, 2); // Scan code set 2
    }

    // TODO Reinitialize the keyboard and 8042 even when not using set2. We should not assume it
    // has a sane state after the boot loader.
    // Also, the key-up code of the last input to grub often comes in just
    // after boot up. We should ignore that (would come as a bonus after
    // resetting the keyboard.)

    // Clear the 8042 input buffer twice for good measure?
    clear_8042_buffer();
    clear_8042_buffer();
}

static int current_writer = -1;
static intptr_t current_reader = 0;
static char have_key = 0;

static void msg_write(int writer, int c) {
    if (c == '\n') {
        current_writer = -1;
    }
    else {
        current_writer = writer;
    }

    putchar(c);
}

static void msg_read(uintptr_t txid) {
    log("msg_read from %lx/%d\n", txid >> 32, msg_dest_fd(txid));

    log("Have reader, key=%d waiting=%lx\n", have_key, current_reader);
    if (have_key) {
        // If we had a reader that should've gotten the character as soon as it
        // was received from the keyboard.
        assert(current_reader == -1);
        send1(MSG_CON_READ, txid, have_key);
        have_key = 0;
        current_reader = -1;
    }
    else {
        current_reader = txid;
    }
}

static bool shift_state = false;

static char map_key(u8 scancode) {
// one key event: optional e0 (or e1/e2), followed by one make/break code
// for now, we ignore all e0 codes and only look at the "normal" code that follows

    if (scancode == 0xe0) {
        return 0;
    }

    const bool press = !(scancode & 0x80);
    const u8 index = scancode & 0x7f;
    if (index > sizeof(keymap)) {
        return 0;
    }

    const char* map = shift_state ? shifted_keymap : keymap;
    const char c = map[index];

    log("key %d %s => %d\n", index, press ? "press" : "release", c);

    if (c == *SPEC_SHIFT) {
        log("shift: state := %d\n", press);
        shift_state = press;
        return 0;
    }

    // Press events don't generate any characters
    if (press) {
        return 0;
    }
    // Otherwise we've generated a normal character, return it.
    return c;
}

static void read_key(void) {
    const u8 scancode = inb(KEY_DATA);

    log("Key scancode received: %x (reader=%lx)\n", scancode, current_reader);

    const char c = map_key(scancode);

    log("ASCII received: %c (%x) (reader=%lx)\n", c, c, current_reader);

    // If the key was successfully mapped to something and a reader is waiting.
    if (c) {
        if (current_reader != -1) {
            send1(MSG_CON_READ, current_reader, c);
            current_reader = -1;
        }
        else {
            have_key = c;
        }
    }
}

static void handle_irq(int fd) {
    while (inb(KEY_CMD) & 1) {
        read_key();
    }

    send0(MSG_IRQ_ACK, fd);
}

static int register_irq(int controller, int irq) {
    ipc_dest_t dest = msg_set_fd(MSG_TX_ACCEPTFD, controller);
    ipc_arg_t arg = irq;
    sendrcv1(MSG_REG_IRQ, dest, &arg);
    return arg;
}

void start() {
    __default_section_init();
    log("started.\n");

    setup_8042();

    const int irq_handle = register_irq(acpica_handle, IRQ_KEYBOARD);

    log("boot complete\n");

    // TODO Implement the grouping to newline feature, setting current_client
    // on the first write and resetting it after printing a newline.
    for (;;) {
        ipc_arg_t arg1, arg2, arg3;
        ipc_dest_t rcpt = msg_set_fd(0, -1);
        log("receiving...\n");
        ipc_msg_t msg = recv3(&rcpt, &arg1, &arg2, &arg3);
        if (msg < 0) {
            log("Got error %ld from recv\n", msg);
            abort();
        }
        const int fd = msg_dest_fd(rcpt);
        log("Received %lx from %#lx/%d args %lx %lx %lx\n", msg, rcpt >> 32, fd, arg1, arg2, arg3);
        switch (msg & 0xff)
        {
        case SYS_PULSE:
            assert(fd == irq_handle);
            handle_irq(fd);
            break;
        case MSG_CON_WRITE:
            msg_write(fd, arg1);
            break;
        case MSG_CON_READ:
            msg_read(rcpt);
            break;
        default:
            log("Unknown message %#lx from %#lx\n", msg & 0xff, rcpt);
        }
    }
}
