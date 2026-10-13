"""List HUD manager objects: address, class, group (+0x14), flags (+0x10), task (+0x18)."""
import struct
from kh2mem import Proc, IMAGE
p = Proc()
vt = {}
for line in open(r"F:/KH2-Coop/research/vtables.tsv"):
    f = line.rstrip("\n").split("\t")
    if f[0] == "VT" and f[3] == "vftable":
        vt[int(f[2], 16)] = f[1]
G = lambda v: hex(v - p.base + IMAGE) if p.base <= v < p.base + 0x3000000 else hex(v)
mgr = p.u64(p.g(0x140ABCD20)); o = p.u64(mgr + 8); counts = {}
rows = []
while o:
    v = p.u64(o); name = vt.get(v - p.base + IMAGE, G(v))
    grp = p.u32(o + 0x14); fl = p.u32(o + 0x10)
    counts[(name, grp)] = counts.get((name, grp), 0) + 1
    rows.append((G(o), name, grp, fl))
    o = p.decode(p.u32(o + 8))
for (name, grp), n in sorted(counts.items(), key=lambda x: x[0][1]):
    print("grupo %-5x %-28s x%d" % (grp, name, n))
print("ctx P1:", [G(p.u64(p.g(a))) for a in (0x140ABC508, 0x140ABC4E0, 0x140ABC4E8, 0x140ABC4F0)])
