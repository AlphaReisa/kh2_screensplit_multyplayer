// Split screen built on the engine's camera-crossfade render mode (render queue 5): the scene is drawn
// twice per frame, once per camera index; the first pass is captured and composited as a sprite.
//  - Start a crossfade with a huge duration and freeze its timer -> dual-camera mode stays on.
//  - Composite only the left half of the captured pass (camera 0 = P1); the right half shows camera 1.
//  - Camera 1 = camera 0 translated by (P2 - P1), written at its commit.
//  - Off-centre projection per pass so each player is centred in its half.
//  - The GS/VU DMA chain of a pass is interpreted at the start of the next section with the renderer's
//    camera index taken from the CURRENT index; present the index of the pass that built the chain.
#include "modules.h"
#include "game.h"

using namespace kc;

namespace split {

static volatile bool g_wanted = false;   // split screen asked for (player 2 joined, or F8)
static volatile bool g_active = false;   // ...and there is someone for camera 2 to follow (not in cutscenes, where
                                         // the party is not spawned)
static bool g_running = false;
static bool g_inComposite = false;
static int  g_lastPassIdx = 0;
static int  g_startFrame = 0;

bool Running() { return g_running; }
bool Wanted() { return g_wanted; }

// ---------------------------------------------------------------- projection shift
// Projection bank: 0x184 bytes per camera index (row-vector 4x4 float matrices):
//   +0x00 GS-space projection (screen centre 2048), +0x84 / +0xc4 / +0x104 clip-space projections,
//   +0x144 NDC->GS viewport (half width at [0][0]). Off-centre shift: M[i][0] += k * M[i][3].
static const int kProjMats[] = {0x00, 0x84, 0xc4, 0x104};
struct ShiftMem { float written[4][4]; float orig[4][4]; bool valid; };
static ShiftMem g_shift[2];

static void ShiftProjection(int idx, float ndc) {
    uintptr_t proj = VA(kh2::ProjBank) + idx * 0x184;
    float halfW = Field<float>(proj, 0x144);   // GS pixels per NDC unit
    ShiftMem& m = g_shift[idx];
    for (int k = 0; k < 4; k++) {
        float* M = reinterpret_cast<float*>(proj + kProjMats[k]);
        float scale = kProjMats[k] == 0x00 ? ndc * halfW : ndc;
        for (int i = 0; i < 4; i++) {
            float cur = M[i * 4 + 0];
            // if the engine didn't rebuild this matrix since last frame, start from the unshifted value
            float base = (m.valid && cur == m.written[k][i]) ? m.orig[k][i] : cur;
            float v = base + scale * M[i * 4 + 3];
            m.orig[k][i] = base;
            m.written[k][i] = v;
            M[i * 4 + 0] = v;
        }
    }
    m.valid = true;
}

static void UnshiftProjection(int idx) {
    ShiftMem& m = g_shift[idx];
    if (!m.valid) return;
    uintptr_t proj = VA(kh2::ProjBank) + idx * 0x184;
    for (int k = 0; k < 4; k++) {
        float* M = reinterpret_cast<float*>(proj + kProjMats[k]);
        for (int i = 0; i < 4; i++)
            if (M[i * 4] == m.written[k][i]) M[i * 4] = m.orig[k][i];
    }
    m.valid = false;
}

// ---------------------------------------------------------------- camera 1 (player 2)
// An independent orbit camera around player 2. Yaw and elevation are its own, turned by player 2's right
// stick, so player 1 moving the game camera no longer drags camera 2 around. It starts from the game
// camera's framing (distance, elevation, target height) and keeps it: player 1's vertical camera input
// changes both elevation and distance of the game camera.
// Slot layout: +0x48 eye, +0x58 target (vec4); the commit rebuilds the view matrix from them.
static bool  g_cam2Init = false;
static float g_cam2Yaw = 0;            // radians, direction from target to eye in the XZ plane
static float g_cam2Dist = 0;           // eye-target distance
static float g_cam2Height = 0;         // target height relative to the player (engine Y down)
static float g_cam2Elev = 0;           // radians, eye elevation above the target (positive = above)
static float g_cam2Target[3];          // smoothed target position

static void UpdateP2Camera() {
    uintptr_t p1 = kh2::Player();
    uintptr_t p2 = party::P2Sora() ? party::P2Sora() : friendctl::Goofy();
    if (!p1 || !p2) return;
    uint8_t* c0 = reinterpret_cast<uint8_t*>(VA(kh2::CamBank));
    uint8_t* c1 = c0 + 0x80;
    const float* eye0 = reinterpret_cast<const float*>(c0 + 0x48);
    const float* tgt0 = reinterpret_cast<const float*>(c0 + 0x58);
    const float* pos1 = kh2::Pos(p1);
    const float* pos2 = kh2::Pos(p2);

    // framing of the game camera
    float off[3] = {eye0[0] - tgt0[0], eye0[1] - tgt0[1], eye0[2] - tgt0[2]};
    float flat = sqrtf(off[0] * off[0] + off[2] * off[2]);
    float dist = sqrtf(flat * flat + off[1] * off[1]);
    float height = tgt0[1] - pos1[1];   // target height relative to the player (engine Y points down)

    if (!g_cam2Init) {
        g_cam2Dist = dist;
        g_cam2Height = height;
        g_cam2Yaw = atan2f(off[0], off[2]);
        g_cam2Elev = atan2f(-off[1], flat);   // engine Y points down
        g_cam2Target[0] = pos2[0];
        g_cam2Target[1] = pos2[1] + height;
        g_cam2Target[2] = pos2[2];
        g_cam2Init = true;
    }
    float want[3] = {pos2[0], pos2[1] + g_cam2Height, pos2[2]};
    g_cam2Yaw += input::P2().camX * 0.045f;   // ~2.7 rad/s at full deflection
    g_cam2Elev -= input::P2().camY * 0.02f;
    g_cam2Elev = g_cam2Elev < -0.15f ? -0.15f : g_cam2Elev > 1.2f ? 1.2f : g_cam2Elev;
    for (int i = 0; i < 3; i++) g_cam2Target[i] += (want[i] - g_cam2Target[i]) * 0.25f;   // light smoothing

    memcpy(c1, c0, 0x80);   // keep the game camera's other parameters (fov, up vector, roll)
    float* eye = reinterpret_cast<float*>(c1 + 0x48);
    float* tgt = reinterpret_cast<float*>(c1 + 0x58);
    for (int i = 0; i < 3; i++) tgt[i] = g_cam2Target[i];
    float horiz = cosf(g_cam2Elev) * g_cam2Dist;
    eye[0] = tgt[0] + sinf(g_cam2Yaw) * horiz;
    eye[1] = tgt[1] - sinf(g_cam2Elev) * g_cam2Dist;
    eye[2] = tgt[2] + cosf(g_cam2Yaw) * horiz;
}

// Player 2's view during player 2's object update. Game logic runs with camera index 0 (player 1's camera);
// what derives from "the current camera" (GetCamera 0x14019ce40 = bank[index]), such as turning the stick
// into a world direction (0x1403a7fb0) or picking targets, has to use camera 2 for the second Sora. Slot 1
// holds camera 2 as last committed and GetCamera answers it during that update. The camera index itself is
// left at 0: other systems keep per-camera state by index (e.g. the 2D layout draw lists, only reset for
// camera 0 outside the render), and switching it overflowed them.
static uint8_t g_cam2Slot[0x80];
static bool g_cam2SlotValid = false;
static uint8_t g_slot1Saved[0x80];
static bool g_p2View = false;

void BeginP2View() {
    if (!g_running || !g_cam2SlotValid || g_p2View) return;
    uint8_t* slot1 = reinterpret_cast<uint8_t*>(VA(kh2::CamBank) + 0x80);
    memcpy(g_slot1Saved, slot1, sizeof(g_slot1Saved));
    memcpy(slot1, g_cam2Slot, sizeof(g_cam2Slot));
    g_p2View = true;
}

void EndP2View() {
    if (!g_p2View) return;
    memcpy(reinterpret_cast<void*>(VA(kh2::CamBank) + 0x80), g_slot1Saved, sizeof(g_slot1Saved));
    g_p2View = false;
}

// GetCamera(char bankSel) = (sel || 0x140ac183c ? bank 0x140ac1020 : 0x140ac1b50) + index * 0x80.
using GetCameraFn = uint8_t* (*)(char sel);
static GetCameraFn o_GetCamera;
static uint8_t* h_GetCamera(char sel) {
    uint8_t* cam = o_GetCamera(sel);
    if (g_p2View && cam == reinterpret_cast<uint8_t*>(VA(kh2::CamBank)) + Global<int32_t>(kh2::CamIndex) * 0x80)
        return reinterpret_cast<uint8_t*>(VA(kh2::CamBank) + 0x80);
    return cam;
}

// Camera 2's view direction straight from its own state: camera slot 1 is only rebuilt during the
// pass-1 commit, so reading it at other points of the frame can return another camera's contents.
bool P2CameraForward(float& fx, float& fz) {
    if (!g_running || !g_cam2Init) return false;
    fx = -sinf(g_cam2Yaw);   // eye -> target
    fz = -cosf(g_cam2Yaw);
    return true;
}

// ---------------------------------------------------------------- hooks
using TaskFn = void (*)(intptr_t task);
using VoidFn = void (*)();
using SpriteFn = void (*)(intptr_t spr);
using FlushDmaFn = uint32_t (*)(char);
static TaskFn o_CrossfadeTick;
static VoidFn o_Composite, o_CamCommit;
static SpriteFn o_SpriteBuild;
static FlushDmaFn o_FlushDma;

// Crossfade timer: frozen while split is on, ended when turned off.
static void h_CrossfadeTick(intptr_t task) {
    float* remaining = reinterpret_cast<float*>(task + 0x18);
    float* total = reinterpret_cast<float*>(task + 0x20);
    if (g_running) {
        if (g_active) *remaining = *total;
        else { *remaining = 0.0f; g_running = false; Log("split terminado"); }
    }
    o_CrossfadeTick(task);
}

// Composite draws the captured pass (camera 0) as a full-screen sprite over the camera-1 pass.
static void h_Composite() {
    g_inComposite = g_running && g_active;
    o_Composite();
    g_inComposite = false;
    if (g_running) Global<int32_t>(kh2::CamIndex) = 0;   // game logic keeps writing camera slot 0
}

// Sprite build: +0x08 u16 w, +0x0c s16 x, +0xd0 uv0, +0xe0 uv1 (u in low 32 bits).
// While compositing keep the left half of the captured image on the left half of the screen.
static void h_SpriteBuild(intptr_t spr) {
    if (!g_inComposite) { o_SpriteBuild(spr); return; }
    uint16_t& w = Field<uint16_t>(spr, 0x08);
    uint32_t& u0 = Field<uint32_t>(spr, 0xd0);
    uint32_t& u1 = Field<uint32_t>(spr, 0xe0);
    uint16_t ow = w;
    uint32_t ou1 = u1;
    w = ow / 2;
    u1 = (u0 + ou1) / 2;
    o_SpriteBuild(spr);
    w = ow;
    u1 = ou1;
}

// Camera commit (runs once per pass with the pass's camera index).
static void h_CamCommit() {
    int idx = Global<int32_t>(kh2::CamIndex);
    if (g_running && idx == 1) UpdateP2Camera();
    o_CamCommit();
    if (g_running && idx == 1) {   // camera 2 as committed: player 2's view for next frame's logic
        memcpy(g_cam2Slot, reinterpret_cast<void*>(VA(kh2::CamBank) + 0x80), sizeof(g_cam2Slot));
        g_cam2SlotValid = true;
    }
    if (idx != 0 && idx != 1) return;
    g_lastPassIdx = idx;
    if (g_running && g_active) ShiftProjection(idx, idx == 0 ? -0.5f : 0.5f);   // P1 left, P2 right
    else UnshiftProjection(idx);
}

static uint32_t h_FlushDma(char kick) {
    if (!(g_running && kick)) return o_FlushDma(kick);
    int32_t& idx = Global<int32_t>(kh2::CamIndex);
    int32_t saved = idx;
    idx = g_lastPassIdx;
    uint32_t r = o_FlushDma(kick);
    idx = saved;
    return r;
}

bool Install() {
    return Hook("CrossfadeTick", 0x1401631e0, "40534883ec20f30f1051180f57db0f2f", (void*)h_CrossfadeTick, (void**)&o_CrossfadeTick)
        && Hook("CrossfadeComposite", 0x140172120, "40534883ec3065488b0425580000008b", (void*)h_Composite, (void**)&o_Composite)
        && Hook("SpriteBuild", 0x14015f940, "0fb641164c8bd10fb65117440fbf490c", (void*)h_SpriteBuild, (void**)&o_SpriteBuild)
        && Hook("CameraCommit", 0x14019cba0, "48895c240848896c2410488974241848", (void*)h_CamCommit, (void**)&o_CamCommit)
        && Hook("FlushDma", 0x1401a0460, "40534883ec200fb6d9e812caffff8bc8", (void*)h_FlushDma, (void**)&o_FlushDma)
        && Hook("GetCamera", 0x14019ce40, "84c9750f380df2499200488d0dff4c92", (void*)h_GetCamera, (void**)&o_GetCamera)
        && CheckBytes(0x140163440, "4883ec380f297424204c8d05c0fcffff");   // StartCrossfade(float), called directly
}

void Frame() {
    static bool joined = false;   // player 2 joining / leaving turns split screen on / off
    if (input::Joined() != joined) {
        joined = !joined;
        g_wanted = joined;
        Log("split %s (jugador 2 %s)", g_wanted ? "ON" : "OFF", joined ? "unido" : "fuera");
    }
    if (KeyPressed(g_cfg.keySplit)) {
        g_wanted = !g_wanted;
        Log("split %s", g_wanted ? "ON" : "OFF");
    }
    // The engine destroys the crossfade task on area changes / cutscenes: detect it and restart.
    if (g_running && g_frame - g_startFrame > 10 && Global<uintptr_t>(kh2::FadeTask) == 0) {
        g_running = false;
        UnshiftProjection(0);
        UnshiftProjection(1);
        Log("tarea de doble camara perdida%s", g_active ? ", reiniciando" : "");
    }
    bool target = party::P2Sora() || (!g_cfg.secondSora && friendctl::Goofy());
    g_active = g_wanted && target;
    if (g_active && !g_running) {
        g_startFrame = g_frame;
        g_cam2Init = false;   // camera 2 starts from the game camera's current yaw
        g_cam2SlotValid = false;
        g_running = true;
        GameFn<void (*)(float)>(0x140163440)(10000.0f);   // very long crossfade = dual camera stays on
    }
}

void Shutdown() {
    g_wanted = false;   // the crossfade tick ends the mode on the game thread
    g_active = false;
    for (int i = 0; i < 100 && g_running; i++) Sleep(10);
}

void ShutdownAfterHooks() {
    UnshiftProjection(0);
    UnshiftProjection(1);
}

}  // namespace split
