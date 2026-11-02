// Player 2's own battle command menu. (Research: research/command_menu.md)
//
// The game has a single YS::FIELD_COMMAND, static at 0x142a10620 (what NOTES called "the player controller"):
// every SORA constructor points obj+0xdc0 (menu update), obj+0xdd0 (command source) and obj+0xdc8 (its lock-on
// block, +0xbe8) at it and binds it to itself. A second Sora therefore ran P1's menu a second time each frame
// (with P2's pad, inside P2's update) and both Soras shared its effects / lock-on state, which crashed the game
// (effect handles killed by one Sora and used by the other's scripts).
//
// Player 2 gets a second FIELD_COMMAND, built with the game's own constructor and initialisation (as the static
// initialiser 0x140024560 does for the first) and bound to the second Sora by hand (the game's Bind 0x1403fd830
// would also set g_CurFieldCommand and HUD slot 0, which belong to P1). P2's own ObjUpdate then updates it (it
// reads P2's pad through obj+0xdb8 = GetPad(0), with pads swapped around that update) and executes its command.
//
// Drawing: dk::COMMAND_DRAW works on globals (instance 0x140abc508, sprite/entry pools 0x140abc4e0/4e8/4f0,
// per-level counters 0x140abc4f8, state 0x140abc510). Player 2's menu has its own set of them, its "HUD
// context", swapped in while P2's menu code runs: P2's object update, the 2D tasks of P2's menu pieces and the
// menu's teardown. Inside that context the game creates P2's own COMMAND_DRAW and pools, its HUD groups 1/2 (menu
// root / entries) are registered as 0x101/0x102 so P1's menu opening or closing does not touch P2's (global
// HUD show/hide, e.g. for cutscenes, still reaches both), and sprite positions move into the right half.
// The context is on only for P2's menu code: FieldCmdUpdate / Open / Close (0x1403fd690 / 0x1403fd7e0 /
// 0x1403fd720) on P2's FIELD_COMMAND, the 2D tasks of P2's menu pieces and its teardown — not for the rest of
// the second Sora's update, which can open other HUD (event dialogs, choices) that must stay the game's.
//
// Entries (dk::COMMAND_ONE) come from a pool with a free list; their release (vtable[0], 0x14015b420), run
// when the HUD manager retires them, pushes the entry back onto the *current* pool's free list. Retiring
// happens outside any player's context, so each entry is released in the context of the pool it lies in.
//
// "The player" for P2's own update and menu is P2: PartyMember(0) (0x1403d5c50, party member i: 0 = g_Player,
// 1..2 = party slots), which the menu uses e.g. for "use item on self", answers the second Sora in P2's context;
// so does PartyMemberById (0x1403d5c70, the party member with a character id, g_Player first): item targets are
// stored as character ids and both Soras share Sora's. "P2's context" here also covers the second Sora's object
// update, where its commands execute.
#include "modules.h"
#include "game.h"
#include <utility>

using namespace kc;

namespace cmdmenu {

static const uintptr_t kFieldCommand = 0x142a10620;   // the game's (P1's) menu
static const uintptr_t kFieldCommandVt = 0x1405c4c68;
static const uintptr_t kDrawContext = 0x140abc4e0;    // 0x140abc4e0 .. 0x140abc510 inclusive
static const size_t kDrawContextSize = 0x31;
static const int kGroupOffset = 0x100;                // P2's HUD groups: 1 -> 0x101, 2 -> 0x102
static const int kHalfScreen = 341;                   // HUD units at 16:9
static const uint64_t kMagic = 0x32504d4e45444d43;    // "CMDMENP2"

// P2's menu and its HUD context in one block that outlives core reloads (P2+0xdc0 points at it).
struct Block {
    alignas(16) uint8_t fieldCommand[0xc80];
    uint8_t drawContext[0x40];
    uint64_t magic;
};
static Block* g_block;
static int g_context;            // >0: P2's HUD context is active
static bool g_inP1Update;        // inside P1's object update (its menu opening / closing its own groups)

static uintptr_t Fc2() { return reinterpret_cast<uintptr_t>(g_block->fieldCommand); }

static void SwapContext() {
    uint8_t* game = reinterpret_cast<uint8_t*>(VA(kDrawContext));
    for (size_t i = 0; i < kDrawContextSize; i++) std::swap(game[i], g_block->drawContext[i]);
}

void Enter() { if (g_block && g_context++ == 0) SwapContext(); }
void Leave() { if (g_block && --g_context == 0) SwapContext(); }
void SetInP1Update(bool on) { g_inP1Update = on; }

template <class Fn> static Fn VFunc(uintptr_t obj, size_t slot) {
    return *reinterpret_cast<Fn*>(Field<uintptr_t>(obj, 0) + slot * 8);
}

// The static initialiser 0x140024560, minus its atexit registration.
static bool Construct() {
    void* mem = VirtualAlloc(nullptr, sizeof(Block), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) return false;
    g_block = static_cast<Block*>(mem);
    Persist().cmdMenu = g_block;   // a reloaded core keeps using it (the game holds pointers into it)
    uintptr_t fc = Fc2();
    GameFn<void (*)(uintptr_t)>(0x1403b1c70)(fc);   // PLAYER_COMMAND constructor
    Field<uintptr_t>(fc, 0) = VA(kFieldCommandVt);
    GameFn<void (*)(uintptr_t)>(0x1403bda50)(fc + 0xb98);
    GameFn<void (*)(uintptr_t)>(0x1403bda50)(fc + 0xbac);
    GameFn<void (*)(uintptr_t)>(0x1403bda50)(fc + 0xbc0);
    GameFn<void (*)(uintptr_t)>(0x1403dcb30)(fc + 0xbe8);   // lock-on block
    Field<uint32_t>(fc, 0xc30) = 0;
    g_block->magic = kMagic;
    Log("menu P2: FIELD_COMMAND propio en %llx", (unsigned long long)fc);
    return true;
}

// Bind P2's menu to the second Sora (the game's Bind 0x1403fd830 minus its P1 globals), after a zone reset.
void BindP2(uintptr_t p2) {
    if (!g_block && !Construct()) { Log("menu P2: sin memoria"); return; }
    uintptr_t fc = Fc2();
    Enter();
    GameFn<void (*)(uintptr_t)>(0x1403fd760)(fc);                 // FieldCmdReset (as on zone load)
    GameFn<void (*)(uintptr_t, uintptr_t)>(0x1403dd5b0)(fc + 0xbe8, p2);   // lock-on owner
    Field<uintptr_t>(p2, 0xdc0) = fc;
    Field<uintptr_t>(p2, 0xdd0) = fc;
    Field<uintptr_t>(p2, 0xdc8) = fc + 0xbe8;
    VFunc<void (*)(uintptr_t, uintptr_t, uintptr_t)>(fc, 1)(fc, p2, Field<uintptr_t>(p2, 0xdb8));   // Init(owner, pad)
    Field<uintptr_t>(fc, 0xc38) = p2;
    GameFn<void (*)(int)>(0x140167280)(1);                   // P2's menu pieces update again (shown on open)
    GameFn<void (*)(int)>(0x140167280)(2);
    if (Field<int32_t>(fc, 0xc40) == 0) {
        Field<uint32_t>(fc, 0xb3c) |= 1;
        Field<uint32_t>(fc, 0x24) &= ~1u;
        Field<uint32_t>(fc, 0xc30) |= 1;
    }
    Leave();
    Log("menu P2 ligado al Sora P2 %llx", Rel(p2));
}

// Before the second Sora is destroyed: the game's Unbind 0x1403fd610 minus its P1 globals; P2's menu is hidden.
void UnbindP2(uintptr_t p2) {
    if (!g_block || Field<uintptr_t>(p2, 0xdc0) != Fc2()) return;
    uintptr_t fc = Fc2();
    Enter();
    GameFn<void (*)(uintptr_t)>(0x140408b90)(fc + 0xb10);   // reaction command
    Field<uint32_t>(fc, 0xb3c) &= ~1u;
    Field<uint32_t>(fc, 0x24) |= 1;
    Field<uint32_t>(fc, 0xc30) &= ~1u;
    GameFn<void (*)(uintptr_t)>(0x1403dd5a0)(fc + 0xbe8);   // lock-on
    VFunc<void (*)(uintptr_t)>(fc, 5)(fc);                   // Term
    Field<uintptr_t>(fc, 0xc38) = 0;
    for (int g : {1, 2}) {                                   // P2's menu (groups remapped in context):
        GameFn<void (*)(int)>(0x140166bd0)(g);               //   hidden
        GameFn<void (*)(int)>(0x1401673b0)(g);               //   and not updated while P2 is away
    }
    Leave();
    Field<uintptr_t>(p2, 0xdc0) = 0;
    Field<uintptr_t>(p2, 0xdd0) = 0;
    Field<uintptr_t>(p2, 0xdc8) = 0;
}

// A second Sora that still uses P1's menu (e.g. found after a core reload) gets P2's.
void Adopt(uintptr_t p2) {
    if (Field<uintptr_t>(p2, 0xdc0) == VA(kFieldCommand)) BindP2(p2);
}

// ---------------------------------------------------------------- hooks
using RegisterFn = void (*)(uintptr_t obj, int prio, int group);
using GroupFn = void (*)(int group);
using VoidFn = void (*)();
using SetPosFn = void (*)(uintptr_t sprite, int x, int y);
using PartyMemberFn = uintptr_t (*)(int i);
using EntryReleaseFn = void (*)(uintptr_t entry);
using PartyMemberByIdFn = uintptr_t (*)(int charId);
static RegisterFn o_Register;
static GroupFn o_Show, o_Hide, o_Resume, o_Pause, o_Kill;
static VoidFn o_DrawTeardown;
static SetPosFn o_SetPos;
static PartyMemberFn o_PartyMember;
static EntryReleaseFn o_EntryRelease;
static PartyMemberByIdFn o_PartyMemberById;

static bool IsMenuGroup(int g) { return g == 1 || g == 2; }

static void h_Register(uintptr_t obj, int prio, int group) {
    if (g_context && IsMenuGroup(group)) group += kGroupOffset;
    o_Register(obj, prio, group);
}

// Inside P2's context the menu's groups are P2's. Outside, a show/hide of the menu groups that does not come
// from P1's own menu (P1's object update) is HUD-wide and reaches P2's menu too.
static void ApplyGroup(GroupFn orig, int g, bool mirror) {
    if (g_context) { orig(IsMenuGroup(g) ? g + kGroupOffset : g); return; }
    orig(g);
    if (mirror && g_block && IsMenuGroup(g) && !g_inP1Update) orig(g + kGroupOffset);
}
static void h_Show(int g) { ApplyGroup(o_Show, g, true); }
static void h_Hide(int g) { ApplyGroup(o_Hide, g, true); }
static void h_Resume(int g) { ApplyGroup(o_Resume, g, true); }
static void h_Pause(int g) { ApplyGroup(o_Pause, g, true); }
static void h_Kill(int g) { ApplyGroup(o_Kill, g, false); }   // kills belong to each menu's own teardown

// HUD teardown of the menu drawing: P2's (in its context) and then P1's.
static void h_DrawTeardown() {
    if (g_block) { Enter(); o_DrawTeardown(); Leave(); }
    o_DrawTeardown();
}

static void h_SetPos(uintptr_t sprite, int x, int y) {
    o_SetPos(sprite, g_context ? x + kHalfScreen : x, y);
}

#ifdef KH2COOP_DEBUG
// Diagnosing: which callers reach the menu helpers, and whether P2's context is on at the time.
static void LogCaller(const char* what, int ctx) {
    static uintptr_t seen[64];
    static int n;
    void* frames[8];
    USHORT c = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
    uintptr_t st[4] = {};
    for (USHORT i = 0, j = 0; i < c && j < 4; i++) {
        uintptr_t f = (uintptr_t)frames[i];
        if (f >= g_base && f < g_base + 0x2c00000) st[j++] = f;
    }
    uintptr_t key = st[0] ^ (st[1] << 1) ^ (uintptr_t)ctx;
    for (int k = 0; k < n; k++) if (seen[k] == key) return;
    if (n < 64) seen[n++] = key;
    Log("%s ctx=%d <- %llx <- %llx <- %llx <- %llx", what, ctx, Rel(st[0]), Rel(st[1]), Rel(st[2]), Rel(st[3]));
}
#endif

#ifdef KH2COOP_DEBUG
using EntryAllocFn = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                   uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static EntryAllocFn o_EntryAlloc;
static uintptr_t h_EntryAlloc(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f,
                              uintptr_t g, uintptr_t h, uintptr_t i, uintptr_t j, uintptr_t k, uintptr_t l) {
    LogCaller("EntryAlloc", g_context);
    uintptr_t r = o_EntryAlloc(a, b, c, d, e, f, g, h, i, j, k, l);
    static int n;
    if (n++ < 400) Log("  entrada %llx ctx=%d pool %llx", (unsigned long long)r, g_context, (unsigned long long)Global<uintptr_t>(0x140abc4f0));
    return r;
}
#endif

static bool P2Code() { return g_context || party::InP2Update(); }

static uintptr_t h_PartyMember(int i) {
#ifdef KH2COOP_DEBUG
    LogCaller("PartyMember", g_context);
#endif
    if (i == 0 && P2Code())
        if (uintptr_t p2 = party::P2Sora()) return p2;
    return o_PartyMember(i);
}

static void h_EntryRelease(uintptr_t entry) {
    int owner = PoolOf(entry);
    bool p2Current = g_context > 0;
    bool swap = g_block && ((owner == 2 && !p2Current) || (owner == 1 && p2Current));
    if (swap) SwapContext();
    o_EntryRelease(entry);
    if (swap) SwapContext();
}

// P2's menu code runs in P2's HUD context.
using FieldCmdFn = void (*)(uintptr_t fc);
static FieldCmdFn o_FieldCmdUpdate, o_FieldCmdOpen, o_FieldCmdClose;
template <FieldCmdFn* O> static void h_FieldCmd(uintptr_t fc) {
    if (!g_block || fc != Fc2()) { (*O)(fc); return; }
    Enter();
    (*O)(fc);
    Leave();
}

// A HUD object of P2's menu (registered in P2's groups): its 2D task runs in P2's context (hud.cpp).
static uintptr_t h_PartyMemberById(int charId) {
    if (P2Code())
        if (uintptr_t p2 = party::P2Sora())
            if (GameFn<uint16_t (*)(uintptr_t)>(0x1403b5b20)(p2) == charId) return p2;
    return o_PartyMemberById(charId);
}

// Which menu's entry pool an object lies in (1 = P1, 2 = P2, 0 = neither).
int PoolOf(uintptr_t o) {
    const size_t span = 0x34 * 0x3c8 + 0x100;
    uintptr_t p1 = Global<uintptr_t>(0x140abc4f0), p2 = 0;
    if (g_block) memcpy(&p2, g_block->drawContext + 0x10, 8);
    if (g_context) std::swap(p1, p2);
    if (p1 && o >= p1 && o < p1 + span) return 1;
    if (p2 && o >= p2 && o < p2 + span) return 2;
    return 0;
}

bool OwnsHudObject(uintptr_t o) {
    int group = o ? Field<int32_t>(o, 0x14) : 0;
    return g_block && (group == 1 + kGroupOffset || group == 2 + kGroupOffset);
}

bool Install() {
    Block* b = static_cast<Block*>(Persist().cmdMenu);
    if (b && b->magic == kMagic) {
        g_block = b;
        Log("menu P2 recuperado en %llx", (unsigned long long)Fc2());
    }
    return Hook("HudObjRegister", 0x140166950, "48895c2408574883ec20448941144c8d", (void*)h_Register, (void**)&o_Register)
        && Hook("HudGroupShow", 0x140167470, "40534883ec208bd933c0660f1f440000", (void*)h_Show, (void**)&o_Show)
        && Hook("HudGroupHide", 0x140166bd0, "40534883ec208bd933c0660f1f440000", (void*)h_Hide, (void**)&o_Hide)
        && Hook("HudGroupResume", 0x140167280, "40534883ec208bd933c0660f1f440000", (void*)h_Resume, (void**)&o_Resume)
        && Hook("HudGroupPause", 0x1401673b0, "40534883ec208bd933c0660f1f440000", (void*)h_Pause, (void**)&o_Pause)
        && Hook("HudGroupKill", 0x140166f30, "48895c2408574883ec208bf933db6690", (void*)h_Kill, (void**)&o_Kill)
        && Hook("CmdDrawTeardown", 0x14015c090, "4883ec3848896c244848897424504c89", (void*)h_DrawTeardown, (void**)&o_DrawTeardown)
        && Hook("CmdEntryRelease", 0x14015b420, "4c8b05c910960048b8793f729aafd5b3", (void*)h_EntryRelease, (void**)&o_EntryRelease)
        && Hook("PartyMember", 0x1403d5c50, "85c97508488b0575a96302c3ffc9e90d", (void*)h_PartyMember, (void**)&o_PartyMember)
        && Hook("PartyMemberById", 0x1403d5c70, "48895c24084889742410574883ec208b", (void*)h_PartyMemberById, (void**)&o_PartyMemberById)
        && Hook("FieldCmdUpdate", 0x1403fd690, "40574883ec204883b9380c000000488b", (void*)h_FieldCmd<&o_FieldCmdUpdate>, (void**)&o_FieldCmdUpdate)
        && Hook("FieldCmdOpen", 0x1403fd7e0, "40534883ec208b81300c0000488bd9d1", (void*)h_FieldCmd<&o_FieldCmdOpen>, (void**)&o_FieldCmdOpen)
        && Hook("FieldCmdClose", 0x1403fd720, "40534883ec208b81300c0000488bd9d1", (void*)h_FieldCmd<&o_FieldCmdClose>, (void**)&o_FieldCmdClose)
        && Hook("SpriteSetPos", 0x14016b330, "8991c0010000448981c4010000c3cccc", (void*)h_SetPos, (void**)&o_SetPos)
#ifdef KH2COOP_DEBUG
        && Hook("CmdEntryAlloc", 0x14015d140, "44894424185556574154415641574883", (void*)h_EntryAlloc, (void**)&o_EntryAlloc)
#endif
        && CheckBytes(0x1403b5b20, "4883ec288b4908e844770f000fb7404c")    // object -> character id
        && CheckBytes(0x1403b1c70, "48895c241048896c2418488974242057")    // PLAYER_COMMAND constructor
        && CheckBytes(0x1403bda50, "40534883ec20488bd933c9e8e0f70e00")
        && CheckBytes(0x1403dcb30, "48895c2408574883ec20488bd933ff89")    // lock-on constructor
        && CheckBytes(0x1403fd760, "48895c2408574883ec2033c0488bf989")    // FieldCmdReset
        && CheckBytes(0x1403dd5b0, "40534883ec20833902488bd948895138")    // lock-on bind
        && CheckBytes(0x140408b90, "40534883ec20488bd9488b49104885c9")    // reaction reset
        && CheckBytes(0x1403dd5a0, "48c7413800000000c3cccccccccccccc");   // lock-on reset
}

}  // namespace cmdmenu
