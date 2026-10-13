"""Read bytes / qwords from the game exe on disk at Ghidra VAs. usage: python exebytes.py q|b VA [count]"""
import struct, sys
d = open(r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\KINGDOM HEARTS II FINAL MIX.exe", "rb").read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; n = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
sec = pe + 24 + opt
def off(va):
    rva = va - 0x140000000
    for i in range(n):
        o = sec + 40 * i; vs, vaddr, rs, rp = struct.unpack_from('<IIII', d, o + 8)
        if vaddr <= rva < vaddr + max(vs, rs): return rp + rva - vaddr
mode, va = sys.argv[1], int(sys.argv[2], 16); cnt = int(sys.argv[3]) if len(sys.argv) > 3 else 16
if mode == "b": print(d[off(va):off(va) + cnt].hex())
else:
    for i in range(cnt): print(hex(va + 8 * i), hex(struct.unpack_from('<Q', d, off(va) + 8 * i)[0]))
