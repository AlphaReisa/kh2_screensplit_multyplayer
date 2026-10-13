// Player-2 input: XInput user 1 (second controller) with keyboard stand-ins for testing, and the
// engine-side pad 1 that the second Sora reads.
//
// The engine keeps 2 pads (GetPad(i) = Pad0 + i*0x68). PadUpdate (0x14039bf00) rebuilds both from
// their raw sources each frame. When the PC layer does not feed pad 1 itself, it is filled here from the
// P2 state; around the P2 Sora's object update the two pads are swapped (see party.cpp).
//
// NOTE (roadmap step 2): pad 1 is currently synthesized at the processed level with bit patterns
// observed on pad 0; the proper path is to feed raw pad 1 with the engine's own conversion.
#include "modules.h"
#include "game.h"

using namespace kc;

namespace input {

static P2State g_p2;
static WORD g_prevButtons;

const P2State& P2() { return g_p2; }

WORD TakePressed() {
    WORD p = g_p2.pressedAccum;
    g_p2.pressedAccum = 0;
    return p;
}

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);
struct XPad { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
static XInputGetStateFn g_xinput;

void Frame() {
    if (!g_xinput) {
        HMODULE m = LoadLibraryA("xinput1_4.dll");
        g_xinput = m ? (XInputGetStateFn)GetProcAddress(m, "XInputGetState") : nullptr;
    }
    float x = 0, y = 0, cx = 0;
    WORD buttons = 0;
    XPad pad{};
    if (g_xinput && g_xinput(1, &pad) == 0) {
        x = pad.lx / 32767.0f;
        y = pad.ly / 32767.0f;
        if (sqrtf(x * x + y * y) < 0.24f) x = y = 0;   // dead zone
        buttons = pad.buttons;
        cx = pad.rx / 32767.0f;
        if (fabsf(cx) < 0.24f) cx = 0;
    }
    // keyboard stand-ins for pad 2: IJKL stick, N/M camera, U = A, O = B, P = X, Ñ = Y, F12 = Back
    if (KeyDown('N')) cx -= 1;
    if (KeyDown('M')) cx += 1;
    if (KeyDown('U')) buttons |= 0x1000;
    if (KeyDown('O')) buttons |= 0x2000;
    if (KeyDown('P')) buttons |= 0x4000;
    if (KeyDown(VK_OEM_3)) buttons |= 0x8000;
    if (KeyDown(VK_F12)) buttons |= 0x0020;
    if (KeyDown('J')) x -= 1;
    if (KeyDown('L')) x += 1;
    if (KeyDown('I')) y += 1;
    if (KeyDown('K')) y -= 1;
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

// ---------------------------------------------------------------- engine pad 1
static const uint64_t kBitJump = 0x01000108;   // what Space produces on pad 0
using VoidFn = void (*)();
static VoidFn o_PadUpdate;
static uint64_t g_pad1Prev;

static void h_PadUpdate() {
    o_PadUpdate();
    uint8_t* pad1 = reinterpret_cast<uint8_t*>(VA(kh2::Pad0)) + kh2::PadSize;
    uintptr_t raw1 = *reinterpret_cast<uintptr_t*>(pad1 + 0x48);
    bool native = raw1 && Field<int32_t>(raw1, 0x4c) == 1;   // PC layer feeds pad 1 itself
    if (native || !party::P2Sora()) return;
    uint64_t cur = 0;
    if (g_p2.buttons & 0x2000) cur |= kBitJump;              // attack is a battle command (party.cpp)
    *reinterpret_cast<uint64_t*>(pad1 + 0x00) = cur;
    *reinterpret_cast<uint64_t*>(pad1 + 0x08) = cur & ~g_pad1Prev;
    *reinterpret_cast<uint64_t*>(pad1 + 0x10) = g_pad1Prev & ~cur;
    *reinterpret_cast<uint64_t*>(pad1 + 0x18) = cur & ~g_pad1Prev;
    g_pad1Prev = cur;
    float* st = reinterpret_cast<float*>(pad1 + 0x30);
    float mag = sqrtf(g_p2.stickX * g_p2.stickX + g_p2.stickY * g_p2.stickY);
    st[0] = g_p2.stickX;
    st[1] = -g_p2.stickY;
    st[2] = 0;
    st[3] = mag > 1 ? 1 : mag;
    *reinterpret_cast<uint32_t*>(pad1 + 0x50) &= ~2u;          // connected
}

void SwapPads() {
    uint8_t* pad0 = reinterpret_cast<uint8_t*>(VA(kh2::Pad0));
    uint8_t tmp[kh2::PadSize];
    memcpy(tmp, pad0, kh2::PadSize);
    memcpy(pad0, pad0 + kh2::PadSize, kh2::PadSize);
    memcpy(pad0 + kh2::PadSize, tmp, kh2::PadSize);
}

bool Install() {
    return Hook("PadUpdate", 0x14039bf00, "48895c240848896c2410488974241857", (void*)h_PadUpdate, (void**)&o_PadUpdate);
}

}  // namespace input
