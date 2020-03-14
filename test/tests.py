#!/usr/bin/env python3

import os
import sys
sys.path.append(os.path.dirname(__file__))

from testlib import *

@with_procs(2)
def test_pulse1(M, A, B):
    result = A.recv(B, 1)
    B.pulse(A, 1) # .expect(0) # TODO pulse doesn't actually return anything
    result.expect(PULSE, B, 1)

if __name__=="__main__":
    main(globals())

