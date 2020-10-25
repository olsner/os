#include "test_common.h"

enum {
    MSG_MAKEFD = MSG_TESTCASE,
    MSG_CLOSEFD,
    MSG_TESTFD,
};

__attribute__((noreturn)) static void proc1_main(void) {
    const int proc2 = 2;
    ipc_arg_t arg = 0;
    // Set up a new socket from A to B
    ipc_msg_t msg = sendrcv1(MSG_MAKEFD, msg_mkdest(proc2, MSG_TX_ACCEPTFD), &arg);
    ASSERT_EQ(MSG_MAKEFD, msg);
    const int fd = arg;
    ASSERT_EQ(0, fd); // First free file descriptor
    // Have B close its end
    msg = sendrcv0(MSG_CLOSEFD, proc2);
    ASSERT_EQ(MSG_CLOSEFD, msg);
    // Send from A: should get -EPIPE
    msg = send0(MSG_TESTFD, fd);
    ASSERT_EQ(msg, -EPIPE);
    // Receive from A: should get -EPIPE
    msg = recv0(fd);
    ASSERT_EQ(msg, -EPIPE);

    pass();
}

__attribute__((noreturn)) static void proc2_main(void) {
    const int proc1 = 1;
    ipc_arg_t arg;
    ipc_dest_t rcpt = proc1;
    ipc_msg_t msg = recv1(&rcpt, &arg);
    ASSERT_EQ(msg_call(MSG_MAKEFD), msg);
    ASSERT_EQ(proc1, msg_dest_fd(rcpt));
    ASSERT_EQ(0, arg);
    int fds[2];
    int res = socketpair(fds);
    ASSERT_EQ(0, res);
    send1(MSG_MAKEFD, MSG_TX_CLOSEFD | rcpt, fds[1]);
    const int fd = fds[0];

    msg = recv0(proc1);
    ASSERT_EQ(msg_call(MSG_CLOSEFD), msg);
    close(fd);
    send0(MSG_CLOSEFD, proc1);

    abort();
}

void start() {
    __default_section_init();
    proc_main();
}
