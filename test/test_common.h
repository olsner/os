#include "../cuser/common.h"
#include <sb1.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define SYS_WRITE SYSCALL_WRITE

enum msg_test {
    MSG_STEP = MSG_USER,
    MSG_RESULT,
};

__attribute__((noreturn)) static void pass(void) {
    puts("PASS");
    abort();
}

__attribute__((noreturn)) static void fail(void) {
    puts("FAIL");
    abort();
}

static void assert_eq(const char *proc, const char *file, int line, const char* exp_s, uintptr_t exp, const char *actual_s, uintptr_t actual) {
    if (exp != actual) {
        printf("[%s] %s:%d: Expected %s == %s (%ld) but got %ld\n", proc, file, line, actual_s, exp_s, exp, actual);
        fail();
    }
}

#define ASSERT_EQ(exp, actual) assert_eq(S_(proc_main), __FILE__, __LINE__, #exp, exp, #actual, actual)

static void wait_for_master(const ipc_dest_t master, const ipc_arg_t step) {
    ipc_dest_t rcpt = master;
    ipc_arg_t arg0;
    ipc_msg_t msg = recv1(&rcpt, &arg0);
    ASSERT_EQ(master, rcpt);
    ASSERT_EQ(msg_call(MSG_STEP), msg);
    ASSERT_EQ(step, arg0);
    send1(MSG_STEP, rcpt, arg0);
}
