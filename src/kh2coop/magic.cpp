// Player 2's magic effects.
//
// Each loaded spell (table 0x142a23a80, 6 entries of 0x50: +0x18 effect container, +0x48 spell data) has one
// effect container, set up on zone load by MagicZoneSetup (0x1403c5a90 -> ContainerInit 0x1402c6320, which
// also links it into the global container list 0x140b63608) and torn down by MagicZoneCleanup (0x1403c5750:
// ContainerClear 0x1402c6490 kills its effects, ContainerUnlink 0x1402c5730). CastMagic (0x1403c5170) starts
// the spell's script (ScriptStart 0x1403e1790(script, entry 0x14, args, count)) with {caster, container, level, ...}, and the
// script spawns / kills its effects by id in that container. With a single container per spell, one Sora's
// cast killed the other's effects of the same spell (e.g. Reflect's barrier), and the first Sora's script
// then asked for the position of an effect that no longer existed (crash in 0x14041e2c0).
//
// Player 2 gets a container of its own per loaded spell, built and torn down with the zone's, and the
// second Sora's casts start their script with it.
#include "modules.h"
#include "game.h"

using namespace kc;

namespace magic {

static const uintptr_t kMagicTable = 0x142a23a80;
static const size_t kEntrySize = 0x50;
static const size_t kContainerOffset = 0x18;
static const size_t kContainerSize = 0x30;
static const int kSpells = 6;

static uintptr_t* Containers() { return Persist().magicContainers; }
static uintptr_t Entry(int i) { return VA(kMagicTable) + i * kEntrySize; }

static void BuildContainers() {
    for (int i = 0; i < kSpells; i++) {
        uintptr_t data = Field<uintptr_t>(Entry(i), kContainerOffset);   // the spell's effect data
        if (!Field<uintptr_t>(Entry(i), 0) || !data || Containers()[i]) continue;
        void* c = GameFn<void* (*)(size_t)>(0x140152430)(kContainerSize);   // game heap (handle-encodable)
        if (!c) continue;
        memset(c, 0, kContainerSize);
        GameFn<void (*)(void*, uintptr_t, int)>(0x1402c6320)(c, data, 0);
        Containers()[i] = reinterpret_cast<uintptr_t>(c);
    }
}

static void DestroyContainers() {
    for (int i = 0; i < kSpells; i++) {
        uintptr_t c = Containers()[i];
        if (!c) continue;
        GameFn<void (*)(uintptr_t)>(0x1402c6490)(c);                      // kill its effects
        if (Field<uintptr_t>(c, 0)) GameFn<void (*)(uintptr_t)>(0x1402c5730)(c);   // unlink from the global list
        GameFn<void (*)(uintptr_t)>(0x140152570)(c);
        Containers()[i] = 0;
    }
}

// ---------------------------------------------------------------- hooks
using VoidFn = void (*)();
using CastMagicFn = void* (*)(uintptr_t caster, uint32_t magic, char flag);
using ScriptStartFn = void (*)(uintptr_t script, uintptr_t entry, uint32_t* args, int count);
static VoidFn o_ZoneSetup, o_ZoneCleanup;
static CastMagicFn o_CastMagic;
static ScriptStartFn o_ScriptStart;
static bool g_p2Cast = false;

static void h_ZoneSetup() {
    o_ZoneSetup();
    for (int i = 0; i < kSpells; i++) Containers()[i] = 0;   // previous zone's were destroyed with it
    BuildContainers();
}

static void h_ZoneCleanup() {
    DestroyContainers();   // before the spells' data goes away
    o_ZoneCleanup();
}

static void* h_CastMagic(uintptr_t caster, uint32_t spell, char flag) {
#ifdef KH2COOP_DEBUG
    Log("CastMagic: lanzador %llx (%s) magia %x", Rel(caster),
        caster == kh2::Player() ? "P1" : caster == party::P2Sora() ? "P2" : "otro", spell);
#endif
    bool p2 = caster && caster == party::P2Sora();
    if (p2) BuildContainers();   // e.g. first cast after a core reload in the middle of a zone
    g_p2Cast = p2;
    void* r = o_CastMagic(caster, spell, flag);
    g_p2Cast = false;
    return r;
}

// Args: `count` {u32 handle/value, u32 tag} pairs: caster, container, level, ...
static void h_ScriptStart(uintptr_t script, uintptr_t entry, uint32_t* args, int count) {
    if (g_p2Cast && (uint32_t)entry == 0x14 && args && count >= 2) {
        uintptr_t container = kh2::HandleToPtr(args[2]);
        for (int i = 0; i < kSpells; i++)
            if (container == Entry(i) + kContainerOffset && Containers()[i]) {
                args[2] = kh2::PtrToHandle(reinterpret_cast<void*>(Containers()[i]));
                break;
            }
    }
    o_ScriptStart(script, entry, args, count);
}

bool Install() {
    return Hook("MagicZoneSetup", 0x1403c5a90, "48895c240848896c2410488974241857", (void*)h_ZoneSetup, (void**)&o_ZoneSetup)
        && Hook("MagicZoneCleanup", 0x1403c5750, "48895c241848896c2420574883ec4048", (void*)h_ZoneCleanup, (void**)&o_ZoneCleanup)
        && Hook("CastMagic", 0x1403c5170, "48895c241048896c2418565741544156", (void*)h_CastMagic, (void**)&o_CastMagic)
        && Hook("ScriptStart", 0x1403e1790, "48895c24084889742410574883ec2041", (void*)h_ScriptStart, (void**)&o_ScriptStart)
        && CheckBytes(0x1402c6320, "40534883ec20488911488d4210488941")    // ContainerInit
        && CheckBytes(0x1402c6490, "48895c2418564883ec20488b5918488b")    // ContainerClear
        && CheckBytes(0x1402c5730, "4889742418574883ec20488b3dc7de89")    // ContainerUnlink
        && CheckBytes(0x140152430, "488bd14533c0488b0de3848600488b01")    // game heap alloc
        && CheckBytes(0x140152570, "488bd1488b0da6838600488b0148ff60");   // game heap free
}

}  // namespace magic
