from testlib import *

@with_procs(1)
def test_fd_order(M, A):
    # fd 1 should point to M in A, so we should get new fds starting at 0, 2, 3, ....
    A.socketpair().expect(0, 0, 2)
    A.close(2).expect(0)
    A.socketpair().expect(0, 2, 3)

# TODO Use metadata in the file for number of processes instead of coding it
# here, then do something like automatically find all test_*.c files in test/
@file_test(2)
def test_close_epipe():
    return "test_close_EPIPE.c"
