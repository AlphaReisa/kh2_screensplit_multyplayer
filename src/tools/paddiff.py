"""Snapshot a memory window twice (idle / while a key is held by another process) and print differing dwords.
usage: python paddiff.py <ghidra_addr_hex> <size_hex> <keys-arg-for-keys.ps1>"""
import subprocess
import sys
import time
import struct
from kh2mem import Proc

p = Proc()
addr = int(sys.argv[1], 16)
size = int(sys.argv[2], 16)
keys = sys.argv[3]
a = p.g(addr)
idle = p.read(a, size)
proc = subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         r"F:\KH2-Coop\src\tools\keys.ps1", "-keys", keys, "-delay", "100"])
time.sleep(1.0)
held = p.read(a, size)
proc.wait()
for off in range(0, size, 4):
    x, y = idle[off:off + 4], held[off:off + 4]
    if x != y:
        print("%x  %s -> %s   (%s -> %s)" % (addr + off, x.hex(), y.hex(),
              struct.unpack("<f", x)[0], struct.unpack("<f", y)[0]))
