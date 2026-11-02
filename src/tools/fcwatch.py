"""Log P1's FIELD_COMMAND flags (+0x24, +0xc30) changes for N seconds."""
import sys, time
from kh2mem import Proc
p = Proc(); fc = p.g(0x142a10620); end = time.time() + float(sys.argv[1]); last = None
while time.time() < end:
    v = (p.u32(fc + 0x24), p.u32(fc + 0xc30), p.u32(p.g(0x142a11400)))
    if v != last: print("%.1f +24=%x +c30=%x 142a11400=%x" % (float(sys.argv[1]) - (end - time.time()), *v), flush=True); last = v
    time.sleep(0.05)
