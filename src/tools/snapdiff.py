"""Snapshot .data qwords / dwords that differ between two moments. usage: python snapdiff.py save NAME | diff A B"""
import sys, pickle
from kh2mem import Proc, IMAGE
p = Proc()
S, E = 0x140713000, 0x142b82000
if sys.argv[1] == "save":
    pickle.dump(p.read(p.g(S), E - S), open(r"F:\KH2-Coop\research\snap_%s.bin" % sys.argv[2], "wb"))
else:
    a = pickle.load(open(r"F:\KH2-Coop\research\snap_%s.bin" % sys.argv[2], "rb"))
    b = pickle.load(open(r"F:\KH2-Coop\research\snap_%s.bin" % sys.argv[3], "rb"))
    keys = [int(x, 16) for x in sys.argv[4:]]   # values of interest (ghidra-space pointers)
    import struct
    for i in range(0, len(a) - 7, 8):
        x, y = struct.unpack_from("<Q", a, i)[0], struct.unpack_from("<Q", b, i)[0]
        if x != y and (not keys or any(v - p.base + IMAGE in keys for v in (x, y))):
            f = lambda v: hex(v - p.base + IMAGE) if p.base <= v < p.base + 0x3000000 else hex(v)
            print(hex(S + i), f(x), "->", f(y))
