"""Sample PS2 ports 0-3 and XInput users 0-3 for N seconds; print changes (stick bytes / LX,LY)."""
import ctypes
import sys
import time
from kh2mem import Proc

x = ctypes.WinDLL("xinput1_4")


class S(ctypes.Structure):
    _fields_ = [("pk", ctypes.c_uint32), ("b", ctypes.c_uint16), ("lt", ctypes.c_uint8), ("rt", ctypes.c_uint8),
                ("lx", ctypes.c_int16), ("ly", ctypes.c_int16), ("rx", ctypes.c_int16), ("ry", ctypes.c_int16)]


p = Proc()
dev = p.u64(p.g(0x14079CF00))
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10
end = time.time() + secs
last = None
while time.time() < end:
    ports = []
    for i in range(4):
        b = p.read(dev + 0x18 + i * 0x44, 0x44)
        ports.append("%s/%d" % (b[2:6].hex(), b[0x1F]))
    xi = []
    for i in range(4):
        s = S()
        r = x.XInputGetState(i, ctypes.byref(s))
        xi.append("%6d,%6d,%04x" % (s.lx, s.ly, s.b) if r == 0 else "  ---  ")
    line = "puertos " + " | ".join(ports) + "   xinput " + " | ".join(xi)
    if line != last:
        print("%5.2f %s" % (secs - (end - time.time()), line))
        last = line
    time.sleep(0.05)
