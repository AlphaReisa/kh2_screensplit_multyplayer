// Goofy driven by player 2 (used when there is no second Sora).
//
// Movement interface of every BTLOBJ: +0x10 desired move vector (x, 0, z, speed). MoveUpdate
// (0x1403d6870) turns the object toward it; ACTION_IDLE slot 7 (0x140422520) picks walk/run from the speed
// and slot 8 (0x140422350) integrates it. The friend AI (0x1401b15f0) rewrites the vector between those
// calls, so P2 input is applied right before each one. While P2 owns Goofy the AI behaviour routine
// (obj+0xe00) is hidden for the duration of the AI update and the catch-up teleport (0x1401b7750) is off.
#include "modules.h"
#include "game.h"

using namespace kc;

namespace friendctl {

static uintptr_t g_goofy = 0;
static bool g_controlled = false;
static WORD g_pending = 0;   // P2 buttons pressed and not yet acted on

uintptr_t Goofy() { return kh2::IsLiveGoofy(g_goofy) ? g_goofy : 0; }
bool Controlled() { return g_controlled; }

void Refresh() {
    static int lastScan = -1000;
    if (kh2::IsLiveGoofy(g_goofy)) return;
    if (g_frame - lastScan < 30) { g_goofy = 0; return; }   // rescan at most twice a second
    lastScan = g_frame;
    uintptr_t prev = g_goofy;
    uintptr_t f[4];
    int n = kh2::FindPoolObjects(kh2::FriendType, f, 4);
    g_goofy = 0;
    for (int i = 0; i < n; i++)
        if (kh2::IsLiveGoofy(f[i])) { g_goofy = f[i]; break; }
    if (g_goofy != prev) Log("P2 (Goofy) = %llx (%d amigos)", Rel(g_goofy), n);
}

static bool IsP2Goofy(intptr_t o) { return g_controlled && o && (uintptr_t)o == g_goofy; }

// Stick -> camera-relative move vector, speed scaled to the object's run speed.
static void ApplyMove(uintptr_t o) {
    const input::P2State& p2 = input::P2();
    float fx, fz;
    if (!split::P2CameraForward(fx, fz)) {   // single screen: the game camera
        const uint8_t* cam = reinterpret_cast<const uint8_t*>(VA(kh2::CamBank));
        const float* eye = reinterpret_cast<const float*>(cam + 0x48);
        const float* tgt = reinterpret_cast<const float*>(cam + 0x58);
        fx = tgt[0] - eye[0];
        fz = tgt[2] - eye[2];
        float fl = sqrtf(fx * fx + fz * fz);
        if (fl < 1e-3f) { fx = 0; fz = 1; fl = 1; }
        fx /= fl; fz /= fl;
    }
    float rx = fz, rz = -fx;   // screen right: forward x up, with the engine's Y pointing down
    float mag = sqrtf(p2.stickX * p2.stickX + p2.stickY * p2.stickY);
    float* mv = reinterpret_cast<float*>(o + kh2::obj::MoveVec);
    mv[0] = fx * p2.stickY + rx * p2.stickX;
    mv[1] = 0;
    mv[2] = fz * p2.stickY + rz * p2.stickX;
    mv[3] = mag * Field<float>(o, kh2::obj::RunSpeed);
}

static void ApplyButtons(uintptr_t o) {
    WORD pr = g_pending;
    g_pending = 0;
    if (pr & 0x1000) kh2::ExecCommand(o, 1);                      // A: Attack
    if ((pr & 0x2000) && kh2::InIdleAction(o)) {                  // B: jump (only on the ground)
        ApplyMove(o);                                             // jump in the stick direction
        kh2::GroundJump(o);
    }
}

// ---------------------------------------------------------------- hooks
using ObjFn = void (*)(intptr_t obj);
using IntObjFn = int (*)(intptr_t obj);
using ActUpdFn = void (*)(void* self, intptr_t obj);
using ActVelFn = void* (*)(void* self, void* out, intptr_t obj, float speed);
static ObjFn o_MoveUpdate, o_FriendAI;
static IntObjFn o_FriendWarp;
static ActUpdFn o_IdleUpdate;
static ActVelFn o_IdleVelocity;

static void h_MoveUpdate(intptr_t o) {
    if (IsP2Goofy(o)) { ApplyMove(o); ApplyButtons(o); }
    o_MoveUpdate(o);
}

static void h_FriendAI(intptr_t o) {
    if (!IsP2Goofy(o)) { o_FriendAI(o); return; }
    void*& behaviour = Field<void*>(o, kh2::obj::FriendBehave);
    void* saved = behaviour;
    behaviour = nullptr;
    o_FriendAI(o);
    behaviour = saved;
}

static int h_FriendWarp(intptr_t o) { return IsP2Goofy(o) ? -1 : o_FriendWarp(o); }

static void h_IdleUpdate(void* self, intptr_t o) {
    if (IsP2Goofy(o)) ApplyMove(o);
    o_IdleUpdate(self, o);
}

static void* h_IdleVelocity(void* self, void* out, intptr_t o, float speed) {
    if (IsP2Goofy(o)) ApplyMove(o);
    return o_IdleVelocity(self, out, o, speed);
}

void Frame() {
    const input::P2State& p2 = input::P2();
    if (party::P2Sora()) {   // a second Sora takes player 2
        g_controlled = false;
        g_pending = 0;
        return;
    }
    bool want = input::Joined();   // player 2 joined (LB + RB) -> Goofy is theirs
#ifdef KH2COOP_DEBUG
    want = want || p2.stickActive || g_controlled;   // keyboard stand-ins take Goofy too
#endif
    if (want != g_controlled) {
        g_controlled = want;
        g_pending = 0;
        Log(want ? "P2 toma el control de Goofy" : "P2 devuelve Goofy a la IA");
    }
    if (g_controlled) g_pending |= p2.pressed;
    if (g_controlled || split::Running() || split::Wanted()) Refresh();
}

bool Install() {
    return Hook("MoveUpdate", 0x1403d6870, "40534883ec20488bd98b09e8f0690d00", (void*)h_MoveUpdate, (void**)&o_MoveUpdate)
        && Hook("IdleUpdate", 0x140422520, "48895c2408574883ec30f30f104a1c33", (void*)h_IdleUpdate, (void**)&o_IdleUpdate)
        && Hook("IdleVelocity", 0x140422350, "488bc4488958084889781055488d68a1", (void*)h_IdleVelocity, (void**)&o_IdleVelocity)
        && Hook("FriendAI", 0x1401b15f0, "40574883ec60f781180e000000000020", (void*)h_FriendAI, (void**)&o_FriendAI)
        && Hook("FriendWarpSpot", 0x1401b7750, "4055564157488d6c24b94881ece00000", (void*)h_FriendWarp, (void**)&o_FriendWarp)
        && CheckBytes(0x140422630, "48895c2408574883ec30f30f10053e14");   // IdleJump (ACTION_IDLE slot 9), called directly
}

}  // namespace friendctl
