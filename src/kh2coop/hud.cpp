// HUD. The battle HUD is built by HudCreate (0x14016c3a0) on the HUD manager (*0x140abcd20: task manager +0,
// object list +8 (next handle at obj+8), allocator +0x28) and torn down by HudDestroy (0x14016c2b0), which
// destroys each piece through its vtable +0x18 (the task manager is reset afterwards).
// Player gauge: dk::GAUGE_PLAYER (vtable 0x1405b2100, 0x45e0 bytes, global 0x140abd248) built by its
// constructor (0x14016b6f0) and Init (0x14017f270(gauge, player)), which binds the player (+0x40) and
// registers the 2D task (id 3) that runs Hud2DTask (0x1401667d0) -> vtable +8 (update + draw) every frame.
//
// Player 2 gets a second GAUGE_PLAYER built the same way, alongside the game's own, for the HUD's lifetime.
// The SORA constructor also re-binds the game's gauge to the Sora it builds: kept on P1.
//
// Notifications: HUD slots 0x140abcfe8[3] (0 = player, 1-2 = friends) each have a flags word (0x140abd000[i]);
// game code raises bits on an object's slot (found by object: HudNotify* 0x140168930 / 0x1401688f0 /
// 0x1401688b0 / 0x140168870 = bits 1..4; HudSetSlot 0x140168850 = bit 0, rebind) and each gauge polls the
// flags of its index (+0x34 + +0x38) in 0x14017ee60 (HudFlagTest 0x1401687f0 / HudFlagClear 0x140168ae0) and
// runs its vtable handlers. Player 2's gauge listens on an index of its own (kP2HudIndex, outside the table):
// notifications for the second Sora are kept in g_p2Flags, and player 1's index-0 flags stay player 1's.
// Bit 0's handler rebinds to slot 0's object, so player 2's rebind is done here and announced with bits 1..4.
//
// Placement: a HUD sprite (dk::Sprite, update 0x14016abf0) is drawn at its layout position + its own
// offset (+0x1c8) + its inherited offset (+0x1c0, rebuilt from the parent +0x1e0 each update; left alone
// when there is no parent). Every gauge piece hangs from the gauge's root sprite (gauge+0x1d40), and the HP
// ring masks (SECTOR_MASK / CIRCLE_MASK) are placed from their anchor sprite's inherited offset only, so the
// whole gauge moves by giving the parentless root an inherited offset. HUD units: 512 wide at 4:3, i.e.
// 683 at 16:9.
// In split screen P1's gauge moves half a screen left, into P1's half; P2's stays in the right half.
#include "modules.h"
#include "game.h"

using namespace kc;

namespace hud {

static const uintptr_t kHudManager = 0x140abcd20;
static const uintptr_t kPlayerGauge = 0x140abd248;   // the game's GAUGE_PLAYER
static const uintptr_t kGaugePlayerVt = 0x1405b2100;
static const size_t kGaugeSize = 0x45e0;

static const size_t kRootSprite = 0x1d40;
static const int kHalfScreen = 341;   // HUD units at 16:9

static uintptr_t g_gauge2;   // player 2's GAUGE_PLAYER (0 when the HUD does not exist)
static const int kP2HudIndex = 0x40;
static volatile uint32_t g_p2Flags;

static void UseP2Index(uintptr_t gauge) { Field<int32_t>(gauge, 0x38) = kP2HudIndex - Field<int32_t>(gauge, 0x34); }

static void SetGaugeOffsetX(uintptr_t gauge, int x) { Field<int32_t>(gauge, kRootSprite + 0x1c0) = x; }

static bool IsPlayerGauge(uintptr_t o) { return o && Field<uintptr_t>(o, 0) == VA(kGaugePlayerVt); }

static void* HudAlloc(size_t size) {
    uintptr_t alloc = Field<uintptr_t>(Global<uintptr_t>(kHudManager), 0x28);
    auto fn = *reinterpret_cast<void* (**)(uintptr_t, size_t, int)>(Field<uintptr_t>(alloc, 0) + 8);
    return fn(alloc, size, 0);
}

static void CreateGauge2(uintptr_t p2) {
    void* mem = HudAlloc(kGaugeSize);
    if (!mem) { Log("HUD P2: sin memoria"); return; }
    memset(mem, 0, kGaugeSize);
    uintptr_t g = GameFn<uintptr_t (*)(void*)>(0x14016b6f0)(mem);
    GameFn<void (*)(uintptr_t, uintptr_t)>(0x14017f270)(g, p2);
    UseP2Index(g);
    g_p2Flags = 0x1e;
    g_gauge2 = g;
    Log("HUD P2: indicador creado en %llx", Rel(g));
}

// After a core reload the gauge may already exist: it is the GAUGE_PLAYER in the HUD's object list that is
// not the game's.
static uintptr_t FindGauge2() {
    uintptr_t mgr = Global<uintptr_t>(kHudManager);
    if (!mgr) return 0;
    for (uintptr_t o = Field<uintptr_t>(mgr, 8); o; o = kh2::HandleToPtr(Field<uint32_t>(o, 8)))
        if (IsPlayerGauge(o) && o != Global<uintptr_t>(kPlayerGauge)) return o;
    return 0;
}

// ---------------------------------------------------------------- hooks
using VoidFn = void (*)();
using TaskFn = void (*)(intptr_t task);
using FlagTestFn = uint32_t (*)(int index, uint32_t bit);
using FlagClearFn = void (*)(int index, uint32_t bit);
using NotifyFn = void (*)(uintptr_t obj);
static FlagTestFn o_FlagTest;
static FlagClearFn o_FlagClear;
static NotifyFn o_Notify[4];
static const uintptr_t kNotify[4] = {0x140168930, 0x1401688f0, 0x1401688b0, 0x140168870};   // bits 1..4

static uint32_t h_FlagTest(int index, uint32_t bit) {
    if (index == kP2HudIndex) return (g_p2Flags >> (bit & 31)) & 1;
    return o_FlagTest(index, bit);
}
static void h_FlagClear(int index, uint32_t bit) {
    if (index == kP2HudIndex) { g_p2Flags &= ~(1u << (bit & 31)); return; }
    o_FlagClear(index, bit);
}
template <int I> static void h_Notify(uintptr_t obj) {
    if (obj && obj == party::P2Sora()) { g_p2Flags |= 2u << I; return; }
    o_Notify[I](obj);
}
static VoidFn o_HudCreate, o_HudDestroy;
static TaskFn o_Hud2DTask;

static void h_HudCreate() {
    o_HudCreate();
    g_gauge2 = 0;
}

static void h_HudDestroy() {
    if (g_gauge2) {   // destroyed like the game's own pieces, before the HUD's allocator goes away
        auto destroy = *reinterpret_cast<void (**)(uintptr_t, int)>(Field<uintptr_t>(g_gauge2, 0) + 0x18);
        destroy(g_gauge2, 1);
        g_gauge2 = 0;
    }
    o_HudDestroy();
}

static void h_Hud2DTask(intptr_t task) {
    uintptr_t o = Field<uintptr_t>(task, 0x20);
#ifdef KH2COOP_DEBUG
    uintptr_t vt = o ? Field<uintptr_t>(o, 0) : 1;
    if (o && (vt < g_base || vt >= g_base + 0x2c00000)) {   // diagnosing: a HUD task whose object was wiped
        static uintptr_t seen[32];
        static int n;
        bool known = false;
        for (int i = 0; i < n; i++) known |= seen[i] == o;
        if (!known && n < 32) {
            seen[n++] = o;
            Log("HUD: tarea %llx con objeto borrado %llx vt %llx pool %d (grupo %d, flags %x) | amigo1 %llx amigo2 %llx jugador %llx",
                (unsigned long long)task, (unsigned long long)o, (unsigned long long)vt, cmdmenu::PoolOf(o), Field<int32_t>(o, 0x14), Field<uint32_t>(o, 0x10),
                Rel(Global<uintptr_t>(0x140abd250)), Rel(Global<uintptr_t>(0x140abd258)), Rel(Global<uintptr_t>(kPlayerGauge)));
            for (int i = 0; i < 3; i++) Log("  tabla HUD[%d] = %llx", i, Rel(Global<uintptr_t>(0x140abcfe8 + i * 8)));
        }
        return;
    }
#endif
    uintptr_t p1 = kh2::Player(), p2 = party::P2Sora();
    if (o && o == Global<uintptr_t>(kPlayerGauge) && p1 && p2 && Field<uintptr_t>(o, 0x40) == p2)
        Field<uintptr_t>(o, 0x40) = p1;
    if (cmdmenu::OwnsHudObject(o)) {   // P2's command menu pieces run in P2's HUD context
        cmdmenu::Enter();
        o_Hud2DTask(task);
        cmdmenu::Leave();
        return;
    }
    o_Hud2DTask(task);
}

void Frame() {
    uintptr_t gauge1 = Global<uintptr_t>(kPlayerGauge);
    if (!gauge1) { g_gauge2 = 0; return; }   // no battle HUD
    SetGaugeOffsetX(gauge1, split::Running() ? -kHalfScreen : 0);
    static bool searched = false;
    if (!g_gauge2 && !searched) {
        searched = true;
        if ((g_gauge2 = FindGauge2())) UseP2Index(g_gauge2);
    }
    uintptr_t p2 = party::P2Sora();
    if (!g_gauge2) {
        if (p2) CreateGauge2(p2);
        return;
    }
    SetGaugeOffsetX(g_gauge2, 0);
    uintptr_t& bound = Field<uintptr_t>(g_gauge2, 0x40);
    if (bound != p2) {   // bind / unbind player 2 (no player: the gauge does not update or draw)
        bound = p2;
        if (p2) {
            Field<int32_t>(g_gauge2, 0x2c) = Field<int32_t>(g_gauge2, 0x30) = *Field<int32_t*>(p2, kh2::obj::Status);
            Field<uint64_t>(g_gauge2, 0x45d4) = 0xffff;   // cached drive bars / form level (as Init leaves them)
            g_p2Flags |= 0x1e;                            // everything changed
        }
    }
}

// Core unload: the game's gauge goes back to its own place.
void Shutdown() {
    if (uintptr_t gauge1 = Global<uintptr_t>(kPlayerGauge)) SetGaugeOffsetX(gauge1, 0);
}

bool Install() {
    return Hook("Hud2DTask", 0x1401667d0, "48895c2408574883ec20488b5920488b", (void*)h_Hud2DTask, (void**)&o_Hud2DTask)
        && Hook("HudFlagTest", 0x1401687f0, "4863c1448bca448bc24183e11f49c1e8", (void*)h_FlagTest, (void**)&o_FlagTest)
        && Hook("HudFlagClear", 0x140168ae0, "4863c14c8d0d16459500448bc283e21f", (void*)h_FlagClear, (void**)&o_FlagClear)
        && Hook("HudNotify1", kNotify[0], "4c8d0db146950033d2498bc14c8d05bd", (void*)h_Notify<0>, (void**)&o_Notify[0])
        && Hook("HudNotify2", kNotify[1], "4c8d0df146950033d2498bc14c8d05fd", (void*)h_Notify<1>, (void**)&o_Notify[1])
        && Hook("HudNotify3", kNotify[2], "4c8d0d3147950033d2498bc14c8d053d", (void*)h_Notify<2>, (void**)&o_Notify[2])
        && Hook("HudNotify4", kNotify[3], "4c8d0d7147950033d2498bc14c8d057d", (void*)h_Notify<3>, (void**)&o_Notify[3])
        && Hook("HudCreate", 0x14016c3a0, "48895c241848897c242041564883ec20", (void*)h_HudCreate, (void**)&o_HudCreate)
        && Hook("HudDestroy", 0x14016c2b0, "40534883ec20488b0da30f950033db48", (void*)h_HudDestroy, (void**)&o_HudDestroy)
        && CheckBytes(0x14016b6f0, "48895c240848896c2410488974241848")    // GAUGE_PLAYER constructor
        && CheckBytes(0x14017f270, "48895c24105556574154415541564157");   // GAUGE_PLAYER Init
}

}  // namespace hud
