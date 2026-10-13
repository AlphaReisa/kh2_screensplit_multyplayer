// Second Sora (player 2), spawned by the engine itself in Goofy's party slot.
//
// While player 2 is joined (and second_sora is on) Goofy's slot resolves to Sora: on area loads the
// spawn lists create the second Sora directly, and joining / leaving in the middle of an area swaps
// Goofy <-> second Sora live (see the swap section).
//
// Spawn lists use pseudo ids 0x237/0x238 = party slot 1/2, resolved by PartySlotObjId(slot) (0x1403c32e0)
// to the world's Goofy/Donald objentry. In coop mode Goofy's slot resolves to Sora's objentry
// (SoraObjId(0) = 0x140405190), so a second YS::SORA is created through the normal spawn path.
//
// The SORA constructor (0x140405030) assumes a single player:
//  - it overwrites g_Player / g_Sora and other "the player" globals -> restored to P1;
//  - StatusSlotFor(charId) (0x1403c04e0) reuses the slot already holding Sora -> P2 gets its own slot.
//
// NOTE (roadmap step 3): the status slot still uses an unused-id trick; to be replaced by the pool API.
#include "modules.h"
#include "game.h"
#include <intrin.h>

using namespace kc;

namespace party {

static bool g_coopMode = false;          // Goofy's party slot resolves to Sora
static const int kGoofySlot = 1;          // party slot 1 = Goofy, 2 = Donald
static bool g_pendingP2 = false;
static bool g_inP2Ctor = false;
static uintptr_t g_p2Sora = 0;

uintptr_t P2Sora() {
    return (kh2::IsLiveSora(g_p2Sora) && g_p2Sora != kh2::Player()) ? g_p2Sora : 0;
}

// ---------------------------------------------------------------- P1 globals
// The SORA constructor binds "the player" globals to whatever Sora it builds. Measured list (v0.36):
//   0x142a105d0 g_Player, 0x142ae9a28 g_Sora, 0x140718cb0 camera target, 0x140abcfe8 HUD party table [0],
//   0x142a10420 player-control pointer, and fields of the single player controller 0x142a10620:
//   +0xae0 (0x142a11100), +0xc20 (0x142a11240), +0xc38 (0x142a11258, its owner).
//   (The HUD's GAUGE_PLAYER +0x40 is kept on P1 by hud.cpp.)
// Each one that points at P2 goes back to P1. (Lists such as the active-object list head 0x142a171c8 are
// left alone: they legitimately point at the new object.)
static const uintptr_t kP1Globals[] = {kh2::PlayerPtr, kh2::SoraPtr, kh2::CameraTarget, 0x140abcfe8,
                                       0x142a10420, kh2::PlayerControl, 0x142a11240, 0x142a11258};
static int g_repairFrames;
static uintptr_t g_repairP1;

static void RepairP1Pointers(uintptr_t p1, uintptr_t p2) {
    if (!p1 || !p2) return;
    for (uintptr_t g : kP1Globals)
        if (Global<uintptr_t>(g) == p2) {
            Global<uintptr_t>(g) = p1;
            Log("  puntero P1 restaurado en %llx", (unsigned long long)g);
        }
}

// ---------------------------------------------------------------- hooks
using SlotIdFn = uint32_t (*)(int slot);
using SoraCtorFn = void* (*)(void* self, void* objentry, int form, uint64_t p4, void* pos, uint32_t yaw);
using StatusSlotFn = uint32_t* (*)(int charId);
using ObjFn = void (*)(intptr_t obj);
static SlotIdFn o_PartySlotObjId;
static SoraCtorFn o_SoraCtor;
static StatusSlotFn o_StatusSlotFor;
static ObjFn o_ObjUpdate;

static uint32_t h_PartySlotObjId(int slot) {
    uint32_t id = o_PartySlotObjId(slot);
    if (!g_coopMode || slot != kGoofySlot || !id) return id;
    uint32_t soraId = GameFn<uint32_t (*)(int)>(0x140405190)(0) & 0xFFFF;
    Log("hueco %d: objentry %x -> Sora %x", slot, id, soraId);
    g_pendingP2 = true;
    return soraId;
}

// ---------------------------------------------------------------- player 2's save data
// A status slot is loaded from (0x1403c1d90) and written back to (0x1403c2120) the character's save record
// (slot +0x250: save 0x1409abda0 + (idx-1)*0x114 via 0x1403a14e0) and, for Sora, the save root (slot +0x26c:
// drive / form gauges at +0x1038..+0x1043). Both are Sora's own: a slot for player 2 bound to them would mix
// HP / MP / Drive with player 1's on every load and write-back. Player 2's slot is bound to player 2's own
// copies instead, made from Sora's the first time (they live in the process-persistent block; the engine's
// handle codec registers that memory region by itself).
static const uintptr_t kSaveRoot = 0x1409abda0;

static void BindP2SaveData(uint32_t* slot, int charId) {
    Persistent& ps = Persist();
    if (!ps.p2SaveReady) {
        void* record = GameFn<void* (*)(void*, int)>(0x1403a14e0)(reinterpret_cast<void*>(VA(kSaveRoot)), charId);
        if (record) memcpy(ps.p2Record, record, 0x114);
        memcpy(ps.p2Root, reinterpret_cast<void*>(VA(kSaveRoot)), sizeof(ps.p2Root));
        ps.p2SaveReady = true;
        Log("datos de partida propios del jugador 2 creados");
    }
    slot[0x94] = kh2::PtrToHandle(ps.p2Record);   // +0x250
    slot[0x9b] = kh2::PtrToHandle(ps.p2Root);     // +0x26c
}

// Status slot pool: 80 x 0x278 at 0x142a17290. Slot fields: +0x250 handle of the character's save data,
// +0x260 character id, +0x264 refcount, +0x268 owner handle.
// Outside the constructor, StatusSlotFor(charId) returns the first slot in pool order with that character id,
// which with two Soras can be player 2's. Player 2's own code (its object update) gets player 2's slot;
// everything else gets player 1's (player 2's slot is hidden from the lookup). E.g. a Drive spends the gauge
// and binds the new form Sora to the slot found here.

static bool g_inP2FormCtor = false;   // constructing player 2's form Sora (Drive / Revert)
static uintptr_t g_formP1;            // player 1 while that runs (the constructor sets g_Player to itself early)

static uint32_t* StatusSlotForImpl(int charId);
static uint32_t* h_StatusSlotFor(int charId) {
    uint32_t* r = StatusSlotForImpl(charId);
#ifdef KH2COOP_DEBUG
    if (charId == 1 && P2Sora()) {
        void* frames[8];
        USHORT c = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
        uintptr_t st[3] = {};
        for (USHORT i = 0, j = 0; i < c && j < 3; i++) {
            uintptr_t f = (uintptr_t)frames[i];
            if (f >= g_base && f < g_base + 0x2c00000) st[j++] = f;
        }
        static uintptr_t seen[64]; static int n;
        uintptr_t key = st[0] ^ (st[1] << 1) ^ (uintptr_t)r;
        bool known = false;
        for (int i = 0; i < n; i++) known |= seen[i] == key;
        if (!known && n < 64) {
            seen[n++] = key;
            Log("StatusSlotFor(1) -> %llx [%s%s] <- %llx <- %llx <- %llx", Rel((uintptr_t)r), InP2Update() ? "update P2" : "",
                g_inP2FormCtor ? "ctor forma P2" : "", Rel(st[0]), Rel(st[1]), Rel(st[2]));
        }
    }
#endif
    return r;
}

// The game's lookup also keeps the slot's reference count, so it always runs; the other player's slot is
// hidden from it (relabelled to a non-character id for the call).
static uint32_t* LookupHiding(uint32_t* hidden, int charId) {
    if (!hidden || (int)hidden[0x98] != charId) return o_StatusSlotFor(charId);
    hidden[0x98] = 0xff;
    uint32_t* r = o_StatusSlotFor(charId);
    hidden[0x98] = charId;
    return r;
}

static uint32_t* StatusSlotForImpl(int charId) {
    if (g_inP2FormCtor)   // the new form Sora takes player 2's slot (its drive / form gauges live there)
        return LookupHiding(g_formP1 ? Field<uint32_t*>(g_formP1, kh2::obj::Status) : nullptr, charId);
    if (!g_inP2Ctor) {
        uintptr_t p2 = (kh2::IsLiveSora(g_p2Sora) && g_p2Sora != kh2::Player()) ? g_p2Sora : 0;
        if (!p2) return o_StatusSlotFor(charId);
        if (InP2Update()) {
            uintptr_t p1 = kh2::Player();
            return LookupHiding(p1 ? Field<uint32_t*>(p1, kh2::obj::Status) : nullptr, charId);
        }
        return LookupHiding(Field<uint32_t*>(p2, kh2::obj::Status), charId);
    }
    uint32_t* slot = o_StatusSlotFor(15);   // id 15: valid range, unused -> forces a fresh slot
    if (slot && slot[0x98] == 15) {
        slot[0x98] = charId;
        BindP2SaveData(slot, charId);
        Log("ficha de batalla propia para Sora P2: %llx", Rel((uintptr_t)slot));
    }
    return slot;
}

// Player 2's spawn data, kept from its last destroyed Sora (a form Sora is not spawned from an entry).
static uintptr_t g_p2SpawnGroup, g_p2SpawnEntry;

// Form change task 0x14040d000 builds the new form Sora here; the current form-change context
// (0x142ae9fa8) holds the Sora being replaced in its first field.
static const uintptr_t kFormCtorReturn = 0x14040d2b1;
static const uintptr_t kFormContext = 0x142ae9fa8;

static void* P2FormCtor(void* self, void* objentry, int form, uint64_t p4, void* pos, uint32_t yaw) {
    uintptr_t player = kh2::Player(), sora = Global<uintptr_t>(kh2::SoraPtr);
    g_formP1 = player;
    g_inP2FormCtor = true;
    void* r = o_SoraCtor(self, objentry, form, p4, pos, yaw);
    g_inP2FormCtor = false;
    Global<uintptr_t>(kh2::PlayerPtr) = player;
    Global<uintptr_t>(kh2::SoraPtr) = sora;
    RepairP1Pointers(player, (uintptr_t)self);
    uintptr_t p2 = (uintptr_t)self;
    Field<uintptr_t>(p2, 0x9e8) = g_p2SpawnGroup;   // so leaving can turn it back into Goofy
    Field<uintptr_t>(p2, 0x9f0) = g_p2SpawnEntry;
    g_p2Sora = p2;
    cmdmenu::BindP2(p2);
    Log("Sora P2 en forma %d: %llx", form, Rel(p2));
    return r;
}

static void* h_SoraCtor(void* self, void* objentry, int form, uint64_t p4, void* pos, uint32_t yaw) {
    uintptr_t formCtx = Global<uintptr_t>(kFormContext);
    if ((uintptr_t)_ReturnAddress() == VA(kFormCtorReturn) && formCtx && g_p2Sora &&
        Field<uintptr_t>(formCtx, 0) == g_p2Sora)
        return P2FormCtor(self, objentry, form, p4, pos, yaw);
    uintptr_t player = kh2::Player();
    uintptr_t sora = Global<uintptr_t>(kh2::SoraPtr);
    // P2 only if a different, live Sora already exists (the real P1 is built first on area load)
    if (!g_pendingP2 || !kh2::IsLiveSora(sora) || sora == (uintptr_t)self)
        return o_SoraCtor(self, objentry, form, p4, pos, yaw);
    g_pendingP2 = false;
    g_inP2Ctor = true;
    void* r = o_SoraCtor(self, objentry, form, p4, pos, yaw);
    g_inP2Ctor = false;
    g_p2Sora = (uintptr_t)self;
    g_repairP1 = sora;
    g_repairFrames = 120;   // some globals are bound right after construction
    RepairP1Pointers(sora, (uintptr_t)self);
    cmdmenu::BindP2((uintptr_t)self);   // its own command menu instead of P1's
    Log("Sora P2 creado en %llx (P1 %llx)", Rel(g_p2Sora), Rel(player));
    return r;
}

// Per-object update. The P2 Sora runs with pad 0 and pad 1 swapped, so all its pad reads (its own command
// menu included) see player 2, in P2's HUD context, so its menu draws on P2's own menu HUD, and with camera 2
// as the current camera, so its movement and targeting are relative to player 2's view.
static bool g_inP2Update = false;
bool InP2Update() { return g_inP2Update; }

static void h_ObjUpdate(intptr_t o) {
    uintptr_t p2 = P2Sora();
    if (p2 && (uintptr_t)o == p2) {
        g_inP2Update = true;
        input::SwapPads();
        cmdmenu::Enter();
        split::BeginP2View();
        o_ObjUpdate(o);
        split::EndP2View();
        cmdmenu::Leave();
        input::SwapPads();
        g_inP2Update = false;
        return;
    }
    bool p1 = (uintptr_t)o == kh2::Player();
    if (p1) cmdmenu::SetInP1Update(true);
    o_ObjUpdate(o);
    if (p1) cmdmenu::SetInP1Update(false);
}

// SORA's destroy (type vtable +0x38 = 0x140404bb0) assumes it is the player: it clears g_Sora and, when
// g_Player is set, 0x1403a8f30 tears down the player (g_Player, camera target, player control, the single
// player command menu). For player 2's Sora: its own menu is unbound, its +0xdf8 effect killed (as
// 0x1403a8f30 does) and the generic object destroy (0x1403d6770) runs; nothing of player 1's is touched.
using SoraDestroyFn = void (*)(uintptr_t type, uintptr_t obj);
static SoraDestroyFn o_SoraDestroy;
static void h_SoraDestroy(uintptr_t type, uintptr_t obj) {
    if (!obj || obj != g_p2Sora) { o_SoraDestroy(type, obj); return; }
    g_p2SpawnGroup = Field<uintptr_t>(obj, 0x9e8);
    g_p2SpawnEntry = Field<uintptr_t>(obj, 0x9f0);
    cmdmenu::UnbindP2(obj);
    if (uintptr_t fx = Field<uintptr_t>(obj, 0xdf8)) {
        GameFn<void (*)(uintptr_t)>(0x1402be8a0)(fx);
        Field<uintptr_t>(obj, 0xdf8) = 0;
    }
    GameFn<void (*)(uintptr_t)>(0x1403d6770)(obj);
}

// The SORA constructor binds every Sora it builds as the player (PlayerBind 0x1403aa850: camera target,
// player-control pointer, the single command menu + HUD slot 0). Not for player 2's Soras: their own menu
// is bound by cmdmenu::BindP2.
using PlayerBindFn = void (*)(uintptr_t obj);
static PlayerBindFn o_PlayerBind;
static void h_PlayerBind(uintptr_t obj) {
    if (g_inP2Ctor || g_inP2FormCtor) return;
    o_PlayerBind(obj);
}

// Player 2's forms take no party members: Donald and Goofy are player 1's. DriveRequirement 0x1404005e0
// (cmd; 0 = available) and AbsorbParty 0x140400b20(form) run from the commanding Sora's update.
using DriveReqFn = int (*)(uintptr_t cmd);
using AbsorbFn = void (*)(int form);
static DriveReqFn o_DriveRequirement;
static AbsorbFn o_AbsorbParty;
static int h_DriveRequirement(uintptr_t cmd) {
    if (InP2Update() && cmd && Field<uint16_t>(cmd, 2) == 4) return 0;
    return o_DriveRequirement(cmd);
}
static void h_AbsorbParty(int form) {
    if (InP2Update()) return;
    o_AbsorbParty(form);
}

// ---------------------------------------------------------------- live swap Goofy <-> P2 Sora
// The same steps as the Drive-form change (0x14040d000): the object's own destroy (type vtable +0x38),
// a new object from the old one's spawn entry through its spawn group, then every engine reference moves
// old -> new. Objects keep their group at +0x9e8 and their entry at +0x9f0 (entry: id +0, position +4,
// rotation +0x10). SpawnFromEntryAt (0x1403fe650) is the area loader's spawn with an explicit position;
// the new object keeps pointing at the area's own entry, as any spawned object does.
static void DestroyObject(uintptr_t o) {
    uintptr_t type = kh2::HandleToPtr(Field<uint32_t>(o, 0));
    auto destroy = *reinterpret_cast<void (**)(uintptr_t, uintptr_t)>(Field<uintptr_t>(type, 0) + 0x38);
    destroy(type, o);
}

static void MoveReferences(uintptr_t from, uintptr_t to) {
    for (uintptr_t f : {0x1403bd2e0, 0x1403ab070, 0x1402c4480, 0x1403d06e0, 0x1403ac320, 0x1403e49b0, 0x14053dae0})
        GameFn<void (*)(uintptr_t, uintptr_t)>(f)(from, to);
    GameFn<void (*)()>(0x1401b7f70)();   // re-bind the party table (player, friends) and the friend brains
}

static uintptr_t SpawnLike(uintptr_t old) {
    uintptr_t group = Field<uintptr_t>(old, 0x9e8);
    void* entry = Field<void*>(old, 0x9f0);
    if (!group || !entry) return 0;
    float pos[4] = {kh2::Pos(old)[0], kh2::Pos(old)[1], kh2::Pos(old)[2], 0};   // where the old one stands
    return GameFn<uintptr_t (*)(void*, uintptr_t, float*)>(0x1403fe650)(entry, group, pos);
}

// Goofy -> second Sora. A friend's destroy does not clear its party-table entry (0x142a239b0[obj+0xdd0];
// the table is only reset on area load), so it is cleared here; the friend constructor takes the first
// free entry, so a returning Goofy gets the same one.
static void GoofyToSora(uintptr_t goofy) {
    int idx = Field<int32_t>(goofy, 0xdd0);
    g_coopMode = true;
    DestroyObject(goofy);
    if (idx >= 0 && idx < 2 && Global<uintptr_t>(0x142a239b0 + idx * 8) == goofy) Global<uintptr_t>(0x142a239b0 + idx * 8) = 0;
    uintptr_t sora = SpawnLike(goofy);
    if (!sora) { Log("cambio Goofy -> Sora: fallo al crear"); return; }
    MoveReferences(goofy, sora);
    Log("Goofy -> Sora P2 (%llx)", Rel(sora));
}

// Second Sora -> Goofy (its destroy goes through h_SoraDestroy).
static void SoraToGoofy(uintptr_t sora) {
    g_coopMode = false;
    DestroyObject(sora);   // player 2's destroy path (h_SoraDestroy)
    g_p2Sora = 0;
    uintptr_t goofy = SpawnLike(sora);
    if (!goofy) { Log("cambio Sora P2 -> Goofy: fallo al crear"); return; }
    MoveReferences(sora, goofy);
    Log("Sora P2 -> Goofy (%llx)", Rel(goofy));
}

static volatile bool g_shuttingDown = false;

static void FollowJoin() {
    bool want = input::Joined() && g_cfg.secondSora && !g_shuttingDown;
    uintptr_t sora = P2Sora();
    if (!kh2::Player()) { g_coopMode = want; return; }   // no area loaded: applies when one loads
    // Like the Drive-form change, a swap waits until the character is standing idle: an object destroyed in
    // the middle of an action leaves that action's pending command / references behind.
    if (!want) {
        if (sora) {   // also a second Sora left over without player 2 (e.g. found after a core reload)
            if (kh2::InIdleAction(sora) || g_shuttingDown) SoraToGoofy(sora);
        } else {
            g_coopMode = false;
        }
        return;
    }
    if (sora || g_coopMode) { g_coopMode = true; return; }
    friendctl::Refresh();
    uintptr_t goofy = friendctl::Goofy();
    if (!goofy) g_coopMode = true;
    else if (kh2::InIdleAction(goofy)) GoofyToSora(goofy);
}

// ---------------------------------------------------------------- frame
// A P2 Sora may already exist when this core is (re)loaded: find it in the pool.
static void FindP2Sora() {
    static int last = -1000;
    if (P2Sora()) return;
    g_p2Sora = 0;
    if (g_frame - last < 60) return;
    last = g_frame;
    uintptr_t found[4];
    int n = kh2::FindPoolObjects(kh2::SoraType, found, 4, kh2::Player());
    for (int i = 0; i < n; i++) {
        if (!kh2::InObjectList(found[i])) continue;
        g_p2Sora = found[i];
        Log("Sora P2 encontrado en %llx", Rel(g_p2Sora));
        cmdmenu::Adopt(g_p2Sora);
        break;
    }
}

// Re-bind the known globals and give an already-spawned P2 its own status slot (after a core reload).
static void RepairExisting() {
    uintptr_t p1 = kh2::Player(), p2 = P2Sora();
    if (!p1 || !p2) return;
    RepairP1Pointers(p1, p2);
    if (Field<uintptr_t>(p2, kh2::obj::Status) != Field<uintptr_t>(p1, kh2::obj::Status)) return;
    uint32_t* old = Field<uint32_t*>(p2, kh2::obj::Status);
    g_inP2Ctor = true;
    uint32_t* slot = h_StatusSlotFor((int)old[0x98]);
    g_inP2Ctor = false;
    if (!slot || slot == old) return;
    GameFn<void (*)(uint32_t*, void*)>(0x1403c1d90)(slot, nullptr);   // stats from save data
    slot[0x9a] = kh2::PtrToHandle(reinterpret_cast<void*>(p2));
    Field<uint32_t*>(p2, kh2::obj::Status) = slot;
    if (old[0x99] > 0) old[0x99]--;
    Log("Sora P2 con ficha propia (HP %u/%u)", slot[0], slot[1]);
}

void Frame() {
    FindP2Sora();
    if (g_repairFrames > 0) { g_repairFrames--; RepairP1Pointers(g_repairP1, P2Sora()); }
    FollowJoin();
    if (KeyPressed(g_cfg.keyRepair)) RepairExisting();
}

// Core unload (loader thread): player 2 leaves first, on the game thread, so nothing of player 2's (its
// command menu above all, which needs this core's context switching) runs once the hooks are gone.
void Shutdown() {
    g_shuttingDown = true;
    for (int i = 0; i < 100 && P2Sora(); i++) Sleep(20);
}

bool Install() {
    return Hook("PartySlotObjId", 0x1403c32e0, "40534883ec208bd9b9ffffffffe82efb", (void*)h_PartySlotObjId, (void**)&o_PartySlotObjId)
        && Hook("SoraCtor", 0x140405030, "48895c2408574883ec30f30f10442468", (void*)h_SoraCtor, (void**)&o_SoraCtor)
        && Hook("StatusSlotFor", 0x1403c04e0, "40574883ec208d41ff8bf983f80e0f87", (void*)h_StatusSlotFor, (void**)&o_StatusSlotFor)
        && Hook("SoraDestroy", 0x140404bb0, "488bca48c7056a4e6e0200000000e96d", (void*)h_SoraDestroy, (void**)&o_SoraDestroy)
        && Hook("PlayerBind", 0x1403aa850, "40534883ec208b15744f6602488bd985", (void*)h_PlayerBind, (void**)&o_PlayerBind)
        && Hook("DriveRequirement", 0x1404005e0, "40535741564883ec300fb741026683f8", (void*)h_DriveRequirement, (void**)&o_DriveRequirement)
        && Hook("AbsorbParty", 0x140400b20, "48895c24084889742410574883ec300f", (void*)h_AbsorbParty, (void**)&o_AbsorbParty)
        && CheckBytes(0x1403d6770, "40554883ec208b8120010000488be9c1")    // generic object destroy
        && CheckBytes(0x1402be8a0, "40534883ec2081490c01000100488bd9")    // effect kill
        && Hook("ObjUpdate", 0x1403bfd30, "40534883ec30488bd90f297424208b49", (void*)h_ObjUpdate, (void**)&o_ObjUpdate)
        && CheckBytes(0x1403d6fc0, "48895c2410574883ec3081a124010000")    // ExecCommand, called directly
        && CheckBytes(0x1403fe650, "48895c24084889742410574883ec20f3")    // SpawnFromEntryAt
        && CheckBytes(0x1401b7f70, "48895c2408488974241048897c241841")    // party table re-bind
        && CheckBytes(0x1403bd2e0, "48895c240848896c2410488974241857");   // first reference mover
}

}  // namespace party
