"""List party-like objects (kn::Friend and player) in the static object pool with brain class and position."""
import struct
from kh2mem import Proc, IMAGE

p = Proc()
vt = {}
for line in open(r"F:/KH2-Coop/research/vtables.tsv"):
    f = line.rstrip("\n").split("\t")
    if f[0] == "VT" and f[3] == "vftable":
        vt[int(f[2], 16)] = f[1]


def cls(addr):
    v = p.u64(addr)
    return vt.get(v - p.base + IMAGE, hex(v - p.base + IMAGE)) if v else "-"


start = p.g(0x140713000)
data = p.read(start, 0x2B82000 - 0x713000)
one = struct.pack("<4f", 1, 1, 1, 1)
i = data.find(one)
while i != -1:
    o = i - 0x30
    if o >= 0:
        h = struct.unpack_from("<I", data, o)[0]
        w = struct.unpack_from("<f", data, o + 0x7C)[0]
        if w == 1.0 and (h & 0x80000000):
            t = p.decode(h)
            c = cls(t) if t else "-"
            if "Friend" in c or "PLAYER" in c or "SORA" in c:
                a = start + o
                brain = p.u64(a + 0xDF8) if "Friend" in c else 0
                pos = struct.unpack_from("<3f", data, o + 0x670)
                print(hex(a - p.base + IMAGE), c, cls(brain) if brain else "", [round(x) for x in pos])
    i = data.find(one, i + 4)
print("g_Player", hex(p.u64(p.g(0x142A105D0)) - p.base + IMAGE))
