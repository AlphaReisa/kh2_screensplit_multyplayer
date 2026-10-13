"""HP/MP of every live Sora (status slot at obj+0x5c0: hp +0, maxhp +4, mp +0x180, maxmp +0x184)."""
from kh2mem import Proc, IMAGE
import struct

p = Proc()
start = p.g(0x140713000)
data = p.read(start, 0x2B82000 - 0x713000)
want = p.encode(p.g(0x140750300))
for o in range(0, len(data) - 0x600, 0x10):
    h = struct.unpack_from("<I", data, o)[0]
    if h != want:
        continue
    if struct.unpack_from("<4f", data, o + 0x30) != (1.0, 1.0, 1.0, 1.0):
        continue
    obj = start + o
    st = p.u64(obj + 0x5C0)
    if not st:
        continue
    print(hex(obj - p.base + IMAGE), "ficha", hex(st - p.base + IMAGE),
          "HP %d/%d" % (p.u32(st), p.u32(st + 4)), "PM %d/%d" % (p.u32(st + 0x180), p.u32(st + 0x184)))
