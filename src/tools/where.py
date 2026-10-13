"""Print Sora and Goofy true positions (+0x670) and their distance. Goofy is found by his AI brain class."""
import math
import struct
from kh2mem import Proc, IMAGE

GOOFY_BRAIN_VT = 0x1405B42D8
FRIEND_TYPE = 0x14071D7C0

p = Proc()


def find_goofy():
    start = p.g(0x140713000)
    data = p.read(start, 0x2B82000 - 0x713000)
    want = p.encode(p.g(FRIEND_TYPE))
    for o in range(0, len(data) - 0xE00, 0x10):
        h = struct.unpack_from("<I", data, o)[0]
        if h != want:
            continue
        brain = struct.unpack_from("<Q", data, o + 0xDF8)[0]
        if brain and p.u64(brain) == p.g(GOOFY_BRAIN_VT):
            return start + o
    return 0


sora = p.u64(p.g(0x142A105D0))
goofy = find_goofy()
s = p.f32(sora + 0x670, 3)
if not goofy:
    print("sora", [round(x) for x in s], "goofy no encontrado")
else:
    g = p.f32(goofy + 0x670, 3)
    print("goofy", [round(x) for x in g], "sora", [round(x) for x in s], "dist", round(math.dist(g, s)),
          "goofy@", hex(goofy - p.base + IMAGE))
