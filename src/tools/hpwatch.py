"""Log HP of P1 / P2 Sora and the HP the two player gauges show, whenever they change.
usage: python hpwatch.py SECS P2ADDR(ghidra)"""
import sys, time
from kh2mem import Proc, IMAGE
p = Proc()
secs = float(sys.argv[1]); P2 = p.g(int(sys.argv[2], 16))
G = lambda v: hex(v - p.base + IMAGE) if v else "0"
end = time.time() + secs; last = None
while time.time() < end:
    P1 = p.u64(p.g(0x142A105D0))
    hp = [p.u32(p.u64(o + 0x5C0)) if o and p.u64(o + 0x5C0) else None for o in (P1, P2)]
    gs = []
    mgr = p.u64(p.g(0x140ABCD20)); o = p.u64(mgr + 8) if mgr else 0
    while o:
        if p.u64(o) == p.g(0x1405B2100):
            pl = p.u64(o + 0x40)
            gs.append("%s->%s %d/%d" % (G(o), "P1" if pl == P1 else "P2" if pl == P2 else G(pl), p.u32(o + 0x2C), p.u32(o + 0x30)))
        o = p.decode(p.u32(o + 8))
    line = "HP P1 %s P2 %s | %s" % (hp[0], hp[1], " ; ".join(gs))
    if line != last:
        print("%5.1f %s" % (secs - (end - time.time()), line), flush=True); last = line
    time.sleep(0.05)
