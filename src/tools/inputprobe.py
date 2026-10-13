"""Full input-path probe. Phases (announced with beeps): 0-5 s idle, 5-12 s move controller A, 12-15 s idle,
15-22 s move controller B. Every 100 ms it records, per phase: which XInput users move, which PC-layer ports
move (and their XInput user), which engine pads move, and the engine's active pad index.
usage: python inputprobe.py   (writes research/inputprobe.txt)"""
import ctypes
import time
import winsound
from kh2mem import Proc

x = ctypes.WinDLL("xinput1_4")


class S(ctypes.Structure):
    _fields_ = [("pk", ctypes.c_uint32), ("b", ctypes.c_uint16), ("lt", ctypes.c_uint8), ("rt", ctypes.c_uint8),
                ("lx", ctypes.c_int16), ("ly", ctypes.c_int16), ("rx", ctypes.c_int16), ("ry", ctypes.c_int16)]


p = Proc()
dev = p.u64(p.g(0x14079CF00))
phases = [(0, 5, "reposo"), (5, 12, "MANDO A"), (12, 15, "reposo"), (15, 22, "MANDO B")]
moved = {name: {"xinput": set(), "puertos": set(), "pads": set(), "activo": set()} for _, _, name in phases}
start = time.time()
cur_phase = None
while True:
    t = time.time() - start
    if t >= 22:
        break
    name = next(n for a, b, n in phases if a <= t < b)
    if name != cur_phase:
        winsound.Beep(1200 if "MANDO" in name else 600, 250)
        print("%5.1f s  -> %s" % (t, name), flush=True)
        cur_phase = name
    m = moved[name]
    for u in range(4):
        s = S()
        if x.XInputGetState(u, ctypes.byref(s)) == 0 and (abs(s.lx) > 12000 or abs(s.ly) > 12000):
            m["xinput"].add(u)
    for i in range(4):
        b = p.read(dev + 0x18 + i * 0x44, 0x44)
        if b[0x1F] == 1 and (abs(b[4] - 0x80) > 0x40 or abs(b[5] - 0x80) > 0x40):
            m["puertos"].add("%d(xi%d)" % (i, b[0x40]))
    for i in range(2):
        st = p.f32(p.g(0x140BF31A0) + i * 0x68 + 0x30, 4)
        if st and st[3] > 0.5:
            m["pads"].add(i)
    m["activo"].add(p.u32(p.g(0x140BF3198)))
    time.sleep(0.1)

with open(r"F:\KH2-Coop\research\inputprobe.txt", "w") as f:
    for _, _, name in phases:
        line = "%-8s xinput %s | puertos %s | pads motor %s | pad activo %s" % (
            name, sorted(moved[name]["xinput"]), sorted(moved[name]["puertos"]),
            sorted(moved[name]["pads"]), sorted(moved[name]["activo"]))
        print(line)
        f.write(line + "\n")
