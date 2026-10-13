// Player-2 input.
//
// Facts (measured in-process, see research/NOTES.md):
//  - The PC input layer polls XInput users through GamePad objects (0x1408bb2a8 + i*0x140, user at +0x10;
//    GamePadRead 0x140134d10 converts XInputGetState into PS2-style port data). InputUpdate (0x140105810)
//    fills one port per GamePad and then moves the first port with input into port 0: whatever
//    controller is touched drives player 1. The engine converts ports 0/1 into pads 0/1.
//  - Steam Input exposes extra virtual XInput users inside the game (one physical controller can appear
//    as users 0 and 1), so XInput user numbers do not identify physical controllers.
//
// Design:
//  - Player 2 joins by holding LB + RB for one second on their controller; every XInput user
//    reporting the identical state at that moment (its virtual duplicates) belongs to player 2.
//    The controller rumbles to confirm (1 s on join, 2 s on leave). The same gesture leaves.
//  - For the game's input layer, player 2's users report a neutral state (XInputGetState hook), so
//    player 2 never drives player 1 and player 1 behaves exactly as in the original game.
//  - After InputUpdate, port 1 is filled from player 2's controller with the layer's own conversion
//    (GamePadRead); the engine turns port 1 into pad 1, which the second Sora reads (pads are swapped
//    around its object update, see party.cpp). Without player 2, port 1 is reported disconnected.
#include "modules.h"
#include "game.h"
#include "MinHook.h"
#include <cstdlib>

using namespace kc;

namespace input {

struct XGamepad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; XGamepad pad; };
enum : WORD { XA = 0x1000, XB = 0x2000, XX = 0x4000, XY = 0x8000, XStart = 0x0010, XBack = 0x0020, XLB = 0x0100, XRB = 0x0200 };

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XState*);
using XInputSetStateFn = DWORD(WINAPI*)(DWORD, void*);
static XInputGetStateFn o_XInputGetState;   // trampoline: real device state
static XInputSetStateFn o_XInputSetState;   // trampoline

static int  g_p2Mask = 0;                    // XInput users that belong to player 2
static int  g_p2User = -1;                   // the one we read player 2 from
static bool g_rawRead = false;               // our own read through the input layer: no suppression

static DWORD WINAPI h_XInputGetState(DWORD user, XState* st) {
    DWORD r = o_XInputGetState(user, st);
    if (r == 0 && st && !g_rawRead && user < 4 && (g_p2Mask >> user & 1)) memset(&st->pad, 0, sizeof(st->pad));
    return r;
}

// Confirmation rumble. The game keeps setting the vibration of its GamePads every frame (usually to
// zero), so while ours runs it overrides the game's writes for those users. It goes to every XInput user
// of that controller: with Steam Input only one of the duplicates may reach the hardware.
static int g_rumbleMask = 0;

struct XVib { WORD l, r; };

static DWORD WINAPI h_XInputSetState(DWORD user, XVib* v) {
    if (user < 4 && (g_rumbleMask >> user & 1)) return 0;
    return o_XInputSetState(user, v);
}

static void Rumble(int mask, WORD strength) {
    g_rumbleMask = strength ? mask : 0;
    XVib v = {strength, strength};
    for (int u = 0; u < 4; u++)
        if (mask >> u & 1) o_XInputSetState(u, &v);
}

// ---------------------------------------------------------------- join / leave
static bool SameState(const XGamepad& a, const XGamepad& b) {
    return a.buttons == b.buttons && abs(a.lt - b.lt) < 40 && abs(a.rt - b.rt) < 40 && abs(a.lx - b.lx) < 2000 && abs(a.ly - b.ly) < 2000 &&
           abs(a.rx - b.rx) < 2000 && abs(a.ry - b.ry) < 2000;
}

static int g_rumbleFrames = 0;
static int g_rumbleUsers = 0;

static void UpdateJoin() {
    static int holdFrames[4];
    XState st[4] = {};
    bool ok[4] = {};
    for (int u = 0; u < 4; u++) ok[u] = o_XInputGetState(u, &st[u]) == 0;

    if (g_p2User >= 0 && !ok[g_p2User]) {
        Log("jugador 2: mando desconectado");
        g_p2Mask = 0;
        g_p2User = -1;
    }
    for (int u = 0; u < 4; u++) {
        bool combo = ok[u] && (st[u].pad.buttons & (XLB | XRB)) == (XLB | XRB);
        holdFrames[u] = combo ? holdFrames[u] + 1 : 0;
        if (holdFrames[u] != 60) continue;                        // held for one second
        if (g_p2User >= 0 && !(g_p2Mask >> u & 1)) continue;     // joined: only player 2's controller can leave
        int users = g_p2Mask >> u & 1 ? g_p2Mask : 0;
        if (users) {
            Log("jugador 2 sale (usuario XInput %d)", u);
            g_p2Mask = 0;
            g_p2User = -1;
            g_rumbleFrames = 120;                                 // leave: 2 s
        } else {
            g_p2Mask = 0;
            for (int v = 0; v < 4; v++)
                if (ok[v] && SameState(st[v].pad, st[u].pad)) g_p2Mask |= 1 << v;   // u and its duplicates
            g_p2User = u;
            Log("jugador 2 se une: usuario XInput %d (usuarios %x)", u, g_p2Mask);
            g_rumbleFrames = 60;                                  // join: 1 s
            users = g_p2Mask;
        }
        g_rumbleUsers = users;
        Rumble(users, 65535);                                     // confirmation on that controller
        break;   // its virtual duplicates reach 60 frames in the same frame: one event only
    }
    if (g_rumbleFrames > 0 && --g_rumbleFrames == 0) Rumble(g_rumbleUsers, 0);
}

// ---------------------------------------------------------------- port 1 <- player 2
// Port (0x44 bytes): +0 u16 buttons (PS2 order), +2..+5 sticks, +0x1f state (1 connected, 2 none).
static uint8_t* Port(int i) {
    uintptr_t dev = Global<uintptr_t>(0x14079cf00);
    return dev ? reinterpret_cast<uint8_t*>(dev + 0x18 + i * 0x44) : nullptr;
}

static uintptr_t GamePadForUser(int user) {
    for (int i = 0; i < 4; i++) {
        uintptr_t gp = VA(0x1408bb2a8) + i * 0x140;
        if (Field<int32_t>(gp, 0x10) == user) return gp;
    }
    return 0;
}

using InputUpdateFn = void (*)(uintptr_t dev);
using GamePadReadFn = void (*)(uintptr_t gamepad, uint8_t* port);
static InputUpdateFn o_InputUpdate;

static void h_InputUpdate(uintptr_t dev) {
    o_InputUpdate(dev);
    uint8_t* p1 = Port(1);
    if (!p1 || Field<int32_t>(dev, 0x14) < 2) return;
    uintptr_t gp = g_p2User >= 0 ? GamePadForUser(g_p2User) : 0;
    if (gp) {
        g_rawRead = true;
        GameFn<GamePadReadFn>(0x140134d10)(gp, p1);   // the layer's own XInput -> port conversion
        g_rawRead = false;
    } else {
        *reinterpret_cast<uint16_t*>(p1) = 0;          // no player 2: port 1 disconnected
        memset(p1 + 2, 0x80, 4);
        p1[0x1f] = 2;
    }
}

// ---------------------------------------------------------------- player-2 state for the mod
static P2State g_p2;
static WORD g_prevButtons;

const P2State& P2() { return g_p2; }

bool Joined() { return g_p2User >= 0; }

WORD TakePressed() {
    WORD p = g_p2.pressedAccum;
    g_p2.pressedAccum = 0;
    return p;
}

static float Axis(SHORT v) {
    float f = v / 32767.0f;
    return fabsf(f) < 0.24f ? 0.0f : (f < -1 ? -1.0f : f > 1 ? 1.0f : f);
}

void Frame() {
    UpdateJoin();
    float x = 0, y = 0, cx = 0, cy = 0;
    WORD buttons = 0;
    XState st{};
    if (g_p2User >= 0 && o_XInputGetState(g_p2User, &st) == 0) {
        x = Axis(st.pad.lx);
        y = Axis(st.pad.ly);
        cx = Axis(st.pad.rx);
        cy = Axis(st.pad.ry);
        buttons = st.pad.buttons;
    }
#ifdef KH2COOP_DEBUG
    // keyboard stand-ins for player 2 (debug builds): IJKL stick, N/M camera, U = A, O = B
    if (KeyDown('N')) cx -= 1;
    if (KeyDown('M')) cx += 1;
    if (KeyDown('U')) buttons |= XA;
    if (KeyDown('O')) buttons |= XB;
    if (KeyDown('J')) x -= 1;
    if (KeyDown('L')) x += 1;
    if (KeyDown('I')) y += 1;
    if (KeyDown('K')) y -= 1;
#endif
    float m = sqrtf(x * x + y * y);
    if (m > 1) { x /= m; y /= m; }
    g_p2.camX = cx < -1 ? -1 : cx > 1 ? 1 : cx;
    g_p2.camY = cy < -1 ? -1 : cy > 1 ? 1 : cy;
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
    HMODULE xi = GetModuleHandleA("xinput1_4.dll");
    if (!xi) xi = LoadLibraryA("xinput1_4.dll");
    void* get = xi ? reinterpret_cast<void*>(GetProcAddress(xi, "XInputGetState")) : nullptr;
    void* set = xi ? reinterpret_cast<void*>(GetProcAddress(xi, "XInputSetState")) : nullptr;
    if (!get || !set || MH_CreateHook(get, (void*)h_XInputGetState, (void**)&o_XInputGetState) != MH_OK ||
        MH_CreateHook(set, (void*)h_XInputSetState, (void**)&o_XInputSetState) != MH_OK) {
        Log("hook XInputGetState/SetState: fallo");
        return false;
    }
    return Hook("InputUpdate", 0x140105810, "4c8bdc564881ecf0000000488b05d639", (void*)h_InputUpdate, (void**)&o_InputUpdate)
        && CheckBytes(0x140134d10, "4c8bdc5355574883ec70488b05d74462");   // GamePadRead, called directly
}

void Shutdown() { Rumble(g_rumbleUsers, 0); }

}  // namespace input
