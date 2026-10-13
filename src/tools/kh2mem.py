"""Read-only memory helper for KH2FM (Steam). Usage: python kh2mem.py [cmd]"""
import ctypes, ctypes.wintypes as wt, struct, sys

PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400
EXE = "KINGDOM HEARTS II FINAL MIX.exe"
IMAGE = 0x140000000

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


def find_pid(name):
    arr = (wt.DWORD * 4096)()
    needed = wt.DWORD()
    psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(needed))
    for pid in arr[: needed.value // 4]:
        h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(512)
        psapi.GetModuleBaseNameW(h, None, buf, 512)
        k32.CloseHandle(h)
        if buf.value.lower() == name.lower():
            return pid
    return None


class Proc:
    def __init__(self):
        self.pid = find_pid(EXE)
        if not self.pid:
            sys.exit("KH2 no está en ejecución")
        self.h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, self.pid)
        mods = (ctypes.c_void_p * 1024)()
        needed = wt.DWORD()
        psapi.EnumProcessModulesEx(self.h, mods, ctypes.sizeof(mods), ctypes.byref(needed), 3)
        self.modules = {}
        for m in mods[: needed.value // 8]:
            buf = ctypes.create_unicode_buffer(512)
            psapi.GetModuleBaseNameW(self.h, ctypes.c_void_p(m), buf, 512)
            self.modules[buf.value.lower()] = m
        self.base = self.modules[EXE.lower()]

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
            return None
        return buf.raw[: got.value]

    def write(self, addr, data):
        """Write bytes into the game (opens a write handle on first use)."""
        if not getattr(self, "hw", None):
            self.hw = k32.OpenProcess(0x0008 | 0x0020 | 0x0010, False, self.pid)   # VM_OPERATION | VM_WRITE | VM_READ
        got = ctypes.c_size_t()
        return bool(k32.WriteProcessMemory(self.hw, ctypes.c_void_p(addr), data, len(data), ctypes.byref(got)))

    def g(self, ghidra_addr):
        """Convert Ghidra address (0x14xxxxxxx) to live address."""
        return self.base + (ghidra_addr - IMAGE)

    def u64(self, a):
        b = self.read(a, 8)
        return struct.unpack("<Q", b)[0] if b else None

    def u32(self, a):
        b = self.read(a, 4)
        return struct.unpack("<I", b)[0] if b else None

    # Engine handles: 0x80000000 | bank << 25 | (ptr & 0x1ffffff); bank table (64 qwords) at 0x142b0d720.
    def decode(self, h):
        if not h:
            return 0
        bank = self.u64(self.g(0x142B0D720) + ((h & 0x7FFFFFFF) >> 25) * 8)
        return bank | (h & 0x1FFFFFF) if bank not in (None, 0xFFFFFFFFFFFFFFFF) else 0

    def encode(self, ptr):
        base = ptr & ~0x1FFFFFF
        for i in range(64):
            b = self.u64(self.g(0x142B0D720) + i * 8)
            if b == base:
                return 0x80000000 | (i << 25) | (ptr & 0x1FFFFFF)
            if b in (None, 0xFFFFFFFFFFFFFFFF):
                break
        return None

    def f32(self, a, n=1):
        b = self.read(a, 4 * n)
        return struct.unpack("<%df" % n, b) if b else None


def hexdump(b, start=0):
    for i in range(0, len(b), 16):
        row = b[i : i + 16]
        fl = struct.unpack("<4f", row) if len(row) == 16 else ()
        print("%06x  %-48s  %s" % (start + i, row.hex(" "), " ".join("%10.3f" % f for f in fl)))


if __name__ == "__main__":
    p = Proc()
    print("pid", p.pid, "base", hex(p.base))
    print("d3d:", [m for m in p.modules if m.startswith(("d3d1", "dxgi"))])
    for name, a in [("g_Player", 0x142A105D0), ("g_Sora", 0x142AE9A28)]:
        print(name, hex(p.u64(p.g(a)) or 0))
    print("camIndex", p.u32(p.g(0x140AC1838)), "dualFlag", p.read(p.g(0x140AC183C), 1))
