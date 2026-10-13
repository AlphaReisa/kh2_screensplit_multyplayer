"""For each thread in a minidump: count game-module (and kh2coop core) addresses found on its stack.
Recursion shows up as a few addresses repeated hundreds of times. usage: python dumpstacks.py DMP"""
import struct, sys, collections
d = open(sys.argv[1], "rb").read()
assert d[:4] == b"MDMP"
nstreams, rva = struct.unpack_from("<II", d, 8)
streams = {}
for i in range(nstreams):
    t, size, r = struct.unpack_from("<III", d, rva + 12 * i)
    streams[t] = (size, r)
# modules (stream 4)
mods = []
size, r = streams[4]
n = struct.unpack_from("<I", d, r)[0]
for i in range(n):
    o = r + 4 + i * 108
    base, msize = struct.unpack_from("<QI", d, o)
    nrva = struct.unpack_from("<I", d, o + 20)[0]
    ln = struct.unpack_from("<I", d, nrva)[0]
    name = d[nrva + 4:nrva + 4 + ln].decode("utf-16le").split("\\")[-1]
    mods.append((base, msize, name))
def where(v):
    for b, s, nm in mods:
        if b <= v < b + s:
            if nm.upper().startswith("KINGDOM"): return "KH2 %x" % (v - b + 0x140000000)
            if "kh2coop" in nm.lower(): return "%s+%x" % (nm, v - b)
    return None
# memory (stream 9 = Memory64List)
mem = []
if 9 in streams:
    size, r = streams[9]
    cnt, base_rva = struct.unpack_from("<QQ", d, r)
    off = base_rva
    for i in range(cnt):
        start, sz = struct.unpack_from("<QQ", d, r + 16 + 16 * i)
        mem.append((start, sz, off)); off += sz
def read(addr, n):
    for s, sz, off in mem:
        if s <= addr and addr + n <= s + sz: return d[off + addr - s: off + addr - s + n]
    return None
size, r = streams[3]
n = struct.unpack_from("<I", d, r)[0]
for i in range(n):
    o = r + 4 + i * 48
    tid = struct.unpack_from("<I", d, o)[0]
    stack_start, stack_size, stack_rva = struct.unpack_from("<QII", d, o + 24)
    data = d[stack_rva:stack_rva + stack_size] if stack_size else b""
    c = collections.Counter()
    for k in range(0, len(data) - 7, 8):
        w = where(struct.unpack_from("<Q", data, k)[0])
        if w: c[w] += 1
    if c:
        top = c.most_common(8)
        print("hilo %d pila %x+%x: %s" % (tid, stack_start, stack_size, ", ".join("%s x%d" % t for t in top)))
