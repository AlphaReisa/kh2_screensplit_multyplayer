"""Sample camera slot 0 (0x80 floats-ish) and projection bank index 0 for N seconds; print the min/max of
every float that changes. usage: python camdiff.py [secs]"""
import struct
import sys
import time
from kh2mem import Proc

p = Proc()
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10
regions = {"cam0": (0x140AC1020, 0x80), "proj0": (0x140AC1840, 0x184)}
lo, hi = {}, {}
end = time.time() + secs
while time.time() < end:
    for name, (va, n) in regions.items():
        b = p.read(p.g(va), n)
        for i in range(0, n - 3, 4):
            v = struct.unpack_from("<f", b, i)[0]
            k = (name, i)
            lo[k] = min(lo.get(k, v), v)
            hi[k] = max(hi.get(k, v), v)
    time.sleep(0.02)
for k in sorted(lo):
    if hi[k] - lo[k] > 1e-4 * max(1, abs(lo[k])):
        print("%-5s +%03x  %12.4f .. %12.4f" % (k[0], k[1], lo[k], hi[k]))
