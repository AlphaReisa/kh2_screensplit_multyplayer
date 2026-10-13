"""Show the action (state) objects of Sora and Goofy: handle at obj+0xC -> object -> vtable class."""
from kh2mem import Proc, IMAGE

p = Proc()
vt = {}
for line in open(r"F:/KH2-Coop/research/vtables.tsv"):
    f = line.rstrip("\n").split("\t")
    if f[0] == "VT" and f[3] == "vftable":
        vt[int(f[2], 16)] = f[1]


def handle(h):
    return p.decode(h)


def cls(addr):
    v = p.u64(addr)
    return vt.get(v - p.base + IMAGE, hex(v - p.base + IMAGE)) if v else "-"


sora = p.u64(p.g(0x142A105D0))
goofy = p.g(0x14253CA20)
for name, o in (("sora", sora), ("goofy", goofy)):
    for off in (0x0, 0x8, 0xC):
        h = p.u32(o + off)
        a = handle(h)
        print(name, hex(off), hex(h), cls(a) if a else "-")
    print(name, "yaw", p.f32(o + 0x68C), "target", p.f32(o + 0x7A8), "move", p.f32(o + 0x10, 4))
