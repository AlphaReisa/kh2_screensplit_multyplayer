"""Build the release binaries and package them: dist/KH2FM-Coop-v<version>.zip
usage: python src/tools/package.py   (from the repository root)"""
import os, re, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src", "kh2coop")
BUILD = os.path.join(SRC, "build")

version = re.search(r'#define KH2COOP_VERSION "([^"]+)"', open(os.path.join(SRC, "core.cpp"), encoding="utf-8").read()).group(1)

for target in ("loader", ""):   # release loader, then release core
    r = subprocess.run(["cmd", "/c", os.path.join(SRC, "build.bat")] + ([target] if target else []), cwd=SRC)
    if r.returncode != 0:
        sys.exit("build failed: " + (target or "core"))

os.makedirs(os.path.join(ROOT, "dist"), exist_ok=True)
out = os.path.join(ROOT, "dist", "KH2FM-Coop-v%s.zip" % version)
files = {
    "dinput8.dll": os.path.join(BUILD, "dinput8.dll"),
    "kh2coop_core.dll": os.path.join(BUILD, "kh2coop_core.dll"),
    "kh2coop.ini": os.path.join(ROOT, "dist-template", "kh2coop.ini"),
    "README.md": os.path.join(ROOT, "README.md"),
    "LICENSE.txt": os.path.join(ROOT, "LICENSE"),
    "THIRD_PARTY_NOTICES.md": os.path.join(ROOT, "THIRD_PARTY_NOTICES.md"),
    "MINHOOK_LICENSE.txt": os.path.join(ROOT, "src", "third_party", "minhook", "LICENSE.txt"),
}
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for name, path in files.items():
        z.write(path, name)
print(out)
