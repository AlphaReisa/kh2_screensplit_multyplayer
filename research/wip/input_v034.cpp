// Player-2 input.
//
// PC input layer (Axa): InputUpdate (0x140105810) fills one PS2-style pad port per controller
// (*0x14079cf00 + 0x18 + port*0x44: u16 buttons, analog bytes, +0x1f state 1 = connected) and then moves
// the first port that has any input into port 0 ("any controller drives player 1"). The engine then
// converts port i -> raw pad i -> GetPad(i) (0x14039bbe0 / 0x14039bf00), natively for both pads.
//
// That swap is disabled (JZ -> JMP at 0x1401059e1, verified and reversible) so controller 1 stays on
// port 0 / pad 0; player 2's port is placed in port 1 -> pad 1 (see "port assignment" below). The second
// Sora reads pad 1 through the engine's normal code paths (pads are swapped around its object update).
#include "modules.h"
#include "game.h"
#include <cstdlib>

using namespace kc;

namespace input {

// ---------------------------------------------------------------- one controller per port
struct BytePatch { uintptr_t va; uint8_t expect, repl; bool on; };
static BytePatch g_noPortSwap = {0x1401059e1, 0x74 /*JZ*/, 0xEB /*JMP*/, false};

static bool SetBytePatch(BytePatch& p, bool on) {
    uint8_t* at = reinterpret_cast<uint8_t*>(VA(p.va));
    if (p.on == on) return true;
    if (*at != (on ? p.expect : p.repl)) { Log("parche %llx: byte inesperado %02x", (unsigned long long)p.va, *at); return false; }
    DWORD old;
    VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &old);
    *at = on ? p.repl : p.expect;
    VirtualProtect(at, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 1);
    p.on = on;
    return true;
}

// ---------------------------------------------------------------- port assignment ("press to join")
// Port layout (0x44 bytes): +0x00 u16 buttons in PS2 order (bit 0 Select, 3 Start, 4 Up, 5 Right, 6 Down,
// 7 Left, 8 L2, 9 R2, 10 L1, 11 R1, 12 Triangle, 13 Circle, 14 Cross, 15 Square), +2/+3 right stick x/y,
// +4/+5 left stick x/y (0..255, 0x80 centre, y down), +0x1f state (1 = connected), +0x40 XInput user.
// One physical controller can feed several ports (virtual duplicates, e.g. Steam Input), so a port is a
// player-2 candidate only if its XInput user differs from port 0's. The first candidate that receives
// input becomes player 2 and is placed in port 1, which the engine converts into pad 1 by itself.
// Without player 2, port 1 is reported disconnected so a duplicate of controller 1 never reaches pad 1.
static const int kPortSize = 0x44;
static int g_p2Port = -1;

static uint8_t* Port(int i) {
    uintptr_t dev = Global<uintptr_t>(0x14079cf00);
    return dev ? reinterpret_cast<uint8_t*>(dev + 0x18 + i * kPortSize) : nullptr;
}
static int PortCount() {
    uintptr_t dev = Global<uintptr_t>(0x14079cf00);
    return dev ? Field<int32_t>(dev, 0x14) : 0;
}
static bool PortConnected(const uint8_t* p) { return p[0x1f] == 1; }
static bool PortHasInput(const uint8_t* p) {
    if (*reinterpret_cast<const uint16_t*>(p)) return true;
    for (int i = 2; i < 6; i++) if (abs((int)p[i] - 0x80) > 0x40) return true;
    return false;
}

using InputUpdateFn = void (*)(uintptr_t dev);
static InputUpdateFn o_InputUpdate;

static void h_InputUpdate(uintptr_t dev) {
    o_InputUpdate(dev);
    int n = PortCount();
    uint8_t* p0 = Port(0);
    if (!p0 || n < 2) return;
    if (g_p2Port >= 0 && (g_p2Port >= n || !PortConnected(Port(g_p2Port)) || Port(g_p2Port)[0x40] == p0[0x40])) {
        Log("jugador 2: mando desconectado (puerto %d)", g_p2Port);
        g_p2Port = -1;
    }
    if (g_p2Port < 0) {
        for (int i = 1; i < n; i++) {
            uint8_t* pi = Port(i);
            if (PortConnected(pi) && pi[0x40] != p0[0x40] && PortHasInput(pi)) {
                g_p2Port = i;
                Log("jugador 2: mando en puerto %d (XInput %d)", i, pi[0x40]);
                break;
            }
        }
    }
    uint8_t* p1 = Port(1);
    if (g_p2Port > 1) memcpy(p1, Port(g_p2Port), kPortSize);
    else if (g_p2Port < 0) {                                  // no player 2: port 1 disconnected
        *reinterpret_cast<uint16_t*>(p1) = 0;
        memset(p1 + 2, 0x80, 4);
        p1[0x1f] = 2;
    }
}

// ---------------------------------------------------------------- player-2 state (from port 1)
static P2State g_p2;
static WORD g_prevButtons;

const P2State& P2() { return g_p2; }

WORD TakePressed() {
    WORD p = g_p2.pressedAccum;
    g_p2.pressedAccum = 0;
    return p;
}

static float Axis(uint8_t v) {
    float f = ((int)v - 0x80) / 127.0f;
    if (fabsf(f) < 0.24f) return 0;                          // dead zone
    return f < -1 ? -1 : f > 1 ? 1 : f;
}

void Frame() {
    float x = 0, y = 0, cx = 0;
    WORD buttons = 0;   // P2State uses XInput-style flags: A 0x1000, B 0x2000, X 0x4000, Y 0x8000, Back 0x20
    const uint8_t* p1 = g_p2Port >= 0 ? Port(1) : nullptr;
    if (p1 && PortConnected(p1)) {
        uint16_t b = *reinterpret_cast<const uint16_t*>(p1);
        if (b & 0x4000) buttons |= 0x1000;   // Cross  -> A
        if (b & 0x2000) buttons |= 0x2000;   // Circle -> B
        if (b & 0x8000) buttons |= 0x4000;   // Square -> X
        if (b & 0x1000) buttons |= 0x8000;   // Triangle -> Y
        if (b & 0x0001) buttons |= 0x0020;   // Select -> Back
        x = Axis(p1[4]);
        y = -Axis(p1[5]);
        cx = Axis(p1[2]);
    }
#ifdef KH2COOP_DEBUG
    // keyboard stand-ins for player 2 (debug builds): IJKL stick, N/M camera, U = A, O = B, F12 = Back
    if (KeyDown('N')) cx -= 1;
    if (KeyDown('M')) cx += 1;
    if (KeyDown('U')) buttons |= 0x1000;
    if (KeyDown('O')) buttons |= 0x2000;
    if (KeyDown(VK_F12)) buttons |= 0x0020;
    if (KeyDown('J')) x -= 1;
    if (KeyDown('L')) x += 1;
    if (KeyDown('I')) y += 1;
    if (KeyDown('K')) y -= 1;
#endif
    float m = sqrtf(x * x + y * y);
    if (m > 1) { x /= m; y /= m; }

    g_p2.camX = cx < -1 ? -1 : cx > 1 ? 1 : cx;
    g_p2.stickX = x;
    g_p2.stickY = y;
    g_p2.stickActive = m > 0.01f;
    g_p2.buttons = buttons;
    g_p2.pressed = buttons & ~g_prevButtons;
    g_p2.pressedAccum |= g_p2.pressed;
    g_prevButtons = buttons;
}

void SwapPads() {
    uint8_t* pad0 = reinterpret_cast<uint8_t*>(VA(kh2::Pad0));
    uint8_t tmp[kh2::PadSize];
    memcpy(tmp, pad0, kh2::PadSize);
    memcpy(pad0, pad0 + kh2::PadSize, kh2::PadSize);
    memcpy(pad0 + kh2::PadSize, tmp, kh2::PadSize);
}

bool Install() {
    return CheckBytes(0x1401059df, "85db7456") && SetBytePatch(g_noPortSwap, true)
        && Hook("InputUpdate", 0x140105810, "4c8bdc564881ecf0000000488b05d639", (void*)h_InputUpdate, (void**)&o_InputUpdate);
}

void Shutdown() { SetBytePatch(g_noPortSwap, false); }

}  // namespace input
