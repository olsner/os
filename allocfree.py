#!/usr/bin/env python

# Shell snippet to process logs for parsing by this script:
# 1. remove the initial memory test printouts
# 2. remove escape sequences (and partial ones?)
# 3. filter out the relevant lines
# cat log2 | tail -n +7574 | sed 's/\x1b\[[^m]*m//g' | sed 's/\x1b.*$//g'  | grep '\(allocate\|free\)_frame' > allocfree2.txt

import sys

last = {}
counts = {}

def int_(s):
    if type(s) is int: return s
    if '=' in s:
        s = s.split('=', 1)[1]
    return int(s, 16)

for line in sys.stdin:
    try:
        if '_frame' not in line:
            continue
        line_ = line.split('_frame', 1)[1]
        fs = line_.split()
        if "free_frame" in line:
            addr = fs[0]
            rip = 0
            act = 0
        else:
            addr = fs[1]
            rip = fs[0]
            act = 1
        addr = int_(addr)
        rip = int_(rip)
        last[addr] = (rip,act)
        #print "%d %#x" % (act, addr)
        counts[(addr,act)] = counts.setdefault((addr,act), 0) + 1
    except Exception, e:
        print >>sys.stderr, e, "in wonky line "+repr(line)+" "+repr(line_)+" "+repr(fs)
        raise

for addr,(rip,act) in last.iteritems():
    print "%d %s addr=%#x rip=%#x" % (act, 'alloc' if act == 1 else 'free', addr, rip)

#for count,addr in sorted([(count,addr) for addr,count in counts.iteritems()]):
#    print "%8d %r" % (count, addr)
