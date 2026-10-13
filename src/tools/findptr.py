"""Find qwords in the exe image (.data) and handle-dwords equal to a given object (ghidra address).
usage: python findptr.py <obj_ghidra_hex> [obj2_ghidra_hex]"""
import struct
import sys
from kh2mem import Proc, IMAGE

p = Proc()
start = p.g(0x140713000)
data = p.read(start, 0x2B82000 - 0x713000)
for arg in sys.argv[1:]:
    obj = p.g(int(arg, 16))
    q = struct.pack("<Q", obj)
    h = p.encode(obj)
    print("== punteros a", arg)
    i = data.find(q)
    while i != -1:
        print("  qword en", hex(start + i - p.base + IMAGE))
        i = data.find(q, i + 1)
    # engine handles (exact encoding via the bank table)
    for o in range(0, len(data) - 4, 4):
        v = struct.unpack_from("<I", data, o)[0]
        if h and v == h:
            a = start + o - p.base + IMAGE
            if not (int(arg, 16) <= a < int(arg, 16) + 0xE18):
                print("  handle en", hex(a), hex(v))
