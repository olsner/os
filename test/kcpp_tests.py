from testlib import *

@with_procs(1)
def test_fd_order(M, A):
    # fd 1 should point to M in A, so we should get new fds starting at 0, 2, 3, ....
    A.socketpair().expect(0, 0, 2)
    A.close(2).expect(0)
    A.socketpair().expect(0, 2, 3)

@with_procs(1)
def test_fd_hmod(M, A):
    # fd 1 should point to M in A, so we should get new fds starting at 0, 2, 3, ....
    A.socketpair().expect(0, 0, 2)
    A.hmod_rename(2, 3).expect(0)
    A.socketpair().expect(0, 2, 4)

@with_procs(1)
def test_hmod_invalid(M, A):
    # fd 1 should point to M in A, so we should get new fds starting at 0, 2, 3, ....
    A.hmod_rename(2, 3).expect(-9)

# TODO test hmod_copy too, and copy+rename
