"""Sample the PS2-style pad ports filled by the PC input layer (*0x14079cf00 + 0x18 + port*0x44) and
print every change for N seconds. usage: python ps2pads.py [seconds]"""
import sys
import time
from kh2mem import Proc

p = Proc()
dev = p.u64(p.g(0x14079CF00))
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 5
print("dispositivo", hex(dev))
prev = [None, None]
prevpad = [None, None]
end = time.time() + secs
while time.time() < end:
    for port in (0, 1):
        b = p.read(dev + 0x18 + port * 0x44, 0x44)
        if b != prev[port]:
            print("%6.2f puerto %d: botones %04x  bytes2-9 %s  conectado %d" % (
                secs - (end - time.time()), port, b[0] | b[1] << 8, b[2:10].hex(" "), b[0x1F]))
            prev[port] = b
    for i in (0, 1):
        a = p.g(0x140BF31A0) + i * 0x68
        cur = p.u64(a)
        st = p.f32(a + 0x30, 4)
        key = (cur, tuple(round(v, 2) for v in st))
        if key != prevpad[i]:
            print("%6.2f   motor pad%d: bits %016x  stick %.2f %.2f mag %.2f  flags %x" % (
                secs - (end - time.time()), i, cur, st[0], st[1], st[3], p.u32(a + 0x50)))
            prevpad[i] = key
    time.sleep(0.02)
