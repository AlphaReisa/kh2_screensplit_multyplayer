"""Rumble XInput users one after another to identify physical controllers.
usage: python rumble.py [user ...]   (default 0 1 2 3)"""
import ctypes
import sys
import time

x = ctypes.WinDLL("xinput1_4")


class Vib(ctypes.Structure):
    _fields_ = [("left", ctypes.c_uint16), ("right", ctypes.c_uint16)]


users = [int(a) for a in sys.argv[1:]] or [0, 1, 2, 3]
for u in users:
    r = x.XInputSetState(u, ctypes.byref(Vib(65535, 65535)))
    print("usuario %d: %s" % (u, "vibrando 2 s" if r == 0 else "no conectado"), flush=True)
    if r == 0:
        time.sleep(2)
        x.XInputSetState(u, ctypes.byref(Vib(0, 0)))
        time.sleep(2)
