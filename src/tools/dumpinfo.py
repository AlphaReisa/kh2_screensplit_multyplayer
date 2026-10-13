"""Minimal minidump reader: exception code/address, faulting module+offset, and a raw stack scan for
return addresses inside the game exe (mapped to Ghidra addresses). usage: python dumpinfo.py <file.dmp>"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
assert d[:4] == b"MDMP"
nstreams, dir_rva = struct.unpack_from("<II", d, 8)
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from("<III", d, dir_rva + i * 12)
    streams[t] = (size, rva)

# module list (type 4)
mods = []
size, rva = streams[4]
n = struct.unpack_from("<I", d, rva)[0]
for i in range(n):
    off = rva + 4 + i * 108
    base, msize = struct.unpack_from("<QI", d, off)
    name_rva = struct.unpack_from("<I", d, off + 20)[0]
    ln = struct.unpack_from("<I", d, name_rva)[0]
    name = d[name_rva + 4:name_rva + 4 + ln].decode("utf-16-le")
    mods.append((base, msize, name.split("\\")[-1]))


def where(a):
    for base, sz, name in mods:
        if base <= a < base + sz:
            if name.lower().startswith("kingdom hearts ii"):
                return "KH2 %x" % (a - base + 0x140000000)
            return "%s+%x" % (name, a - base)
    return "?"


# exception stream (type 6)
size, rva = streams[6]
tid = struct.unpack_from("<I", d, rva)[0]
code, flags, rec, addr = struct.unpack_from("<IIQQ", d, rva + 8)
nparams = struct.unpack_from("<I", d, rva + 8 + 24)[0]
params = struct.unpack_from("<2Q", d, rva + 8 + 32)
ctx_size, ctx_rva = struct.unpack_from("<II", d, rva + 8 + 152)
print("excepcion %08x en %x (%s) params %s" % (code, addr, where(addr), [hex(x) for x in params[:nparams]]))
rsp = struct.unpack_from("<Q", d, ctx_rva + 0x98)[0]
rip = struct.unpack_from("<Q", d, ctx_rva + 0xF8)[0]
regs = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"]
vals = struct.unpack_from("<8Q", d, ctx_rva + 0x78)
print("rip", hex(rip), where(rip))
print(" ".join("%s=%x" % (r, v) for r, v in zip(regs, vals)))

# memory list (type 5 MemoryListStream or 9 Memory64ListStream): find the stack range and scan
def read_mem(addr, length):
    if 5 in streams:
        size, rva = streams[5]
        n = struct.unpack_from("<I", d, rva)[0]
        for i in range(n):
            start, dsize, drva = struct.unpack_from("<QII", d, rva + 4 + i * 16)
            if start <= addr < start + dsize:
                o = drva + (addr - start)
                return d[o:o + min(length, start + dsize - addr)]
    return b""


stk = read_mem(rsp, 0x800)
print("pila (direcciones de retorno en modulos):")
for i in range(0, len(stk) - 8, 8):
    v = struct.unpack_from("<Q", stk, i)[0]
    w = where(v)
    if w != "?":
        print("  rsp+%x  %s" % (i, w))
