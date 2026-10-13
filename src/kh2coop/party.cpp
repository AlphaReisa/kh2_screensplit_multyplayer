// Second Sora (player 2), spawned by the engine itself in Goofy's party slot.
//
// Spawn lists use pseudo ids 0x237/0x238 = party slot 1/2, resolved by PartySlotObjId(slot) (0x1403c32e0)
// to the world's Goofy/Donald objentry. In coop mode Goofy's slot resolves to Sora's objentry
// (SoraObjId(0) = 0x140405190), so a second YS::SORA is created through the normal spawn path.
//
// The SORA constructor (0x140405030) assumes a single player:
//  - it overwrites g_Player / g_Sora and other "the player" globals -> restored to P1;
//  - StatusSlotFor(charId) (0x1403c04e0) reuses the slot already holding Sora -> P2 gets its own slot.
//
// NOTE (roadmap step 3): the P1-pointer repair still uses a .data snapshot diff and the status slot uses
// an unused-id trick; both are to be replaced by explicit handling of each global / the pool API.
#include "modules.h"
#include "game.h"

using namespace kc;

namespace party {

static bool g_coopMode = false;
static const int kGoofySlot = 1;          // party slot 1 = Goofy, 2 = Donald
static bool g_pendingP2 = false;
static bool g_inP2Ctor = false;
static uintptr_t g_p2Sora = 0;

uintptr_t P2Sora() {
    return (kh2::IsLiveSora(g_p2Sora) && g_p2Sora != kh2::Player()) ? g_p2Sora : 0;
}

// ---------------------------------------------------------------- P1 pointer repair
static uint64_t* g_dataSnap;
static size_t g_dataSnapLen;
static int g_repairFrames;
static uintptr_t g_repairP1;

static void SnapshotData() {
    size_t len = (VA(kh2::DataEnd) - VA(kh2::DataStart)) / 8;
    if (!g_dataSnap) g_dataSnap = (uint64_t*)VirtualAlloc(nullptr, len * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_dataSnap) return;
    memcpy(g_dataSnap, reinterpret_cast<void*>(VA(kh2::DataStart)), len * 8);
    g_dataSnapLen = len;
}

// Every qword that went from P1 to P2 since the snapshot is put back to P1 (camera target, controller...).
static void RepairP1Pointers(uintptr_t p1, uintptr_t p2) {
    if (!g_dataSnap || !p1 || !p2) return;
    uint64_t* cur = reinterpret_cast<uint64_t*>(VA(kh2::DataStart));
    for (size_t i = 0; i < g_dataSnapLen; i++) {
        if (cur[i] != p2 || g_dataSnap[i] != p1) continue;
        uintptr_t a = VA(kh2::DataStart) + i * 8;
        if (a >= p2 && a < p2 + 0xe18) continue;   // inside the P2 object itself
        cur[i] = p1;
        Log("  puntero P1 restaurado en %llx", Rel(a));
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

// Status slot pool: 80 x 0x278 at 0x142a17290. Slot fields: +0x250 handle of the character's save data,
// +0x260 character id, +0x264 refcount, +0x268 owner handle.
static uint32_t* h_StatusSlotFor(int charId) {
    if (!g_inP2Ctor) return o_StatusSlotFor(charId);
    uint32_t* slot = o_StatusSlotFor(15);   // id 15: valid range, unused -> forces a fresh slot
    if (slot && slot[0x98] == 15) {
        slot[0x98] = charId;
        void* data = GameFn<void* (*)(void*, int)>(0x1403a14e0)(reinterpret_cast<void*>(VA(0x1409abda0)), charId);
        slot[0x94] = kh2::PtrToHandle(data);
        Log("ficha de batalla propia para Sora P2: %llx", Rel((uintptr_t)slot));
    }
    return slot;
}

static void* h_SoraCtor(void* self, void* objentry, int form, uint64_t p4, void* pos, uint32_t yaw) {
    uintptr_t player = kh2::Player();
    uintptr_t sora = Global<uintptr_t>(kh2::SoraPtr);
    // P2 only if a different, live Sora already exists (the real P1 is built first on area load)
    if (!g_pendingP2 || !kh2::IsLiveSora(sora) || sora == (uintptr_t)self)
        return o_SoraCtor(self, objentry, form, p4, pos, yaw);
    g_pendingP2 = false;
    SnapshotData();
    g_inP2Ctor = true;
    void* r = o_SoraCtor(self, objentry, form, p4, pos, yaw);
    g_inP2Ctor = false;
    if (player) Global<uintptr_t>(kh2::PlayerPtr) = player;
    if (sora) Global<uintptr_t>(kh2::SoraPtr) = sora;
    g_p2Sora = (uintptr_t)self;
    g_repairP1 = sora;
    g_repairFrames = 120;   // some globals are bound right after construction
    RepairP1Pointers(sora, (uintptr_t)self);
    Log("Sora P2 creado en %llx (P1 %llx)", Rel(g_p2Sora), Rel(player));
    return r;
}

// "Attack" normally goes through the (single, P1) command menu, so P2 issues battle command 1 directly.
static void P2Commands(uintptr_t p2) {
    WORD pr = input::TakePressed();
    if (pr & 0x1000) kh2::ExecCommand(p2, 1);   // A: Attack
    // Magic stays disabled until the P2 command menu enumerates P2's real (loaded) spells: casting a
    // spell that is not in the loaded-magic table dereferences NULL in 0x1403c5170 (crash 13-10 14:55).
    if (pr & 0xC000) Log("P2 magia: desactivada hasta tener menu de comandos propio");
}

// Per-object update: the P2 Sora runs with pad 0 and pad 1 swapped, so all its pad reads see player 2.
static void h_ObjUpdate(intptr_t o) {
    uintptr_t p2 = P2Sora();
    if (!p2 || (uintptr_t)o != p2) { o_ObjUpdate(o); return; }
    P2Commands(p2);
    input::SwapPads();
    o_ObjUpdate(o);
    input::SwapPads();
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
        break;
    }
}

// Re-bind the known globals and give an already-spawned P2 its own status slot (after a core reload).
static void RepairExisting() {
    uintptr_t p1 = kh2::Player(), p2 = P2Sora();
    if (!p1 || !p2) return;
    for (uintptr_t k : {kh2::CameraTarget, kh2::PlayerControl})
        if (Global<uintptr_t>(k) == p2) { Global<uintptr_t>(k) = p1; Log("%llx = P1", (unsigned long long)k); }
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
    if (KeyPressed(g_cfg.keyCoopMode)) {
        g_coopMode = !g_coopMode;
        Log("modo Sora P2 %d (se aplica al cargar zona)", g_coopMode);
    }
    if (KeyPressed(g_cfg.keyRepair)) RepairExisting();
}

bool Install() {
    g_coopMode = g_cfg.coopMode;
    return Hook("PartySlotObjId", 0x1403c32e0, "40534883ec208bd9b9ffffffffe82efb", (void*)h_PartySlotObjId, (void**)&o_PartySlotObjId)
        && Hook("SoraCtor", 0x140405030, "48895c2408574883ec30f30f10442468", (void*)h_SoraCtor, (void**)&o_SoraCtor)
        && Hook("StatusSlotFor", 0x1403c04e0, "40574883ec208d41ff8bf983f80e0f87", (void*)h_StatusSlotFor, (void**)&o_StatusSlotFor)
        && Hook("ObjUpdate", 0x1403bfd30, "40534883ec30488bd90f297424208b49", (void*)h_ObjUpdate, (void**)&o_ObjUpdate)
        && CheckBytes(0x1403d6fc0, "48895c2410574883ec3081a124010000");   // ExecCommand, called directly
}

}  // namespace party
