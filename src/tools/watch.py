"""Write kh2coop_watch.txt for the debug core's F9 hardware watchpoints.
usage: python watch.py [ms] <expr> <r|w> <len> [<expr> <r|w> <len> ...]
  expr: ghidra address (0x14...), 'ptr:<ghidra>+<off>' (deref a global then add), or 'live:<hex>'.
example (PS2 pad ports): python watch.py 400 ptr:0x14079cf00+0x18 w 2 ptr:0x14079cf00+0x5c w 2"""
import sys
from kh2mem import Proc

GAME_DIR = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-"

p = Proc()
args = sys.argv[1:]
ms = None
if args and args[0].isdigit():
    ms = int(args.pop(0))
lines = []
while len(args) >= 3:
    expr, rw, ln = args[:3]
    args = args[3:]
    if expr.startswith("ptr:"):
        g, off = expr[4:].split("+") if "+" in expr else (expr[4:], "0")
        a = p.u64(p.g(int(g, 16))) + int(off, 16)
    elif expr.startswith("live:"):
        a = int(expr[5:], 16)
    else:
        a = p.g(int(expr, 16))
    lines.append("%x %s %s%s" % (a, rw, ln, (" %d" % ms) if ms and not lines else ""))
open(GAME_DIR + r"\kh2coop_watch.txt", "w").write("\n".join(lines) + "\n")
print("\n".join(lines))
