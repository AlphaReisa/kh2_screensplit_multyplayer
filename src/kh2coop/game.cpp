#include "game.h"

using namespace kc;

namespace kh2 {

// Engine handles are 32-bit compressed pointers: 0x80000000 | bank << 25 | (ptr & 0x1ffffff), with a
// table of 64 32-MB-aligned bases at 0x142b0d720. Always go through the engine's own codec.
uintptr_t HandleToPtr(uint32_t h) {
    return GameFn<uintptr_t (*)(uint32_t)>(0x1404ad270)(h);
}

uint32_t PtrToHandle(const void* p) {
    return GameFn<uint32_t (*)(const void*)>(0x1404ad240)(p);
}

// Handle of a static object (type objects / action singletons), encoded once by the engine.
static uint32_t StaticHandle(uintptr_t ghidra) {
    return PtrToHandle(reinterpret_cast<const void*>(VA(ghidra)));
}

uintptr_t Player() { return Global<uintptr_t>(PlayerPtr); }

bool IsType(uintptr_t o, uintptr_t typeObj) {
    if (!o || IsBadReadPtr(reinterpret_cast<void*>(o), 0x10)) return false;
    return Field<uint32_t>(o, obj::TypeHandle) == StaticHandle(typeObj);
}

// Objects stay in the static pool after destruction, so "alive" means linked in the active list.
bool InObjectList(uintptr_t o) {
    static int cacheFrame = -1;
    static uintptr_t cacheObj;
    static bool cacheRes;
    if (cacheFrame == g_frame && cacheObj == o) return cacheRes;
    bool found = false;
    uintptr_t cur = Global<uintptr_t>(ObjectListHead);
    for (int guard = 0; cur && guard < 4096; guard++) {
        if (cur == o) { found = true; break; }
        cur = HandleToPtr(Field<uint32_t>(cur, obj::ListNext));
    }
    cacheFrame = g_frame; cacheObj = o; cacheRes = found;
    return found;
}

bool IsLiveSora(uintptr_t o) { return IsType(o, SoraType) && InObjectList(o); }

bool IsLiveGoofy(uintptr_t o) {
    if (!IsType(o, FriendType)) return false;
    uintptr_t brain = Field<uintptr_t>(o, obj::FriendBrain);
    if (brain < 0x10000 || IsBadReadPtr(reinterpret_cast<void*>(brain), 8)) return false;
    return *reinterpret_cast<uintptr_t*>(brain) == VA(GoofyBrainVt) && InObjectList(o);
}

// Scan the static object pool for live-looking models of a type (scale (1,1,1,1), position w = 1).
int FindPoolObjects(uintptr_t typeObj, uintptr_t* out, int max, uintptr_t skip) {
    const uint32_t want = StaticHandle(typeObj);
    int n = 0;
    for (uintptr_t a = VA(DataStart); a < VA(DataEnd) && n < max; a += 0x10) {
        if (*reinterpret_cast<uint32_t*>(a) != want || a == skip) continue;
        const float* sc = reinterpret_cast<const float*>(a + obj::Scale);
        if (sc[0] == 1.0f && sc[1] == 1.0f && sc[2] == 1.0f && Pos(a)[3] == 1.0f) out[n++] = a;
    }
    return n;
}

intptr_t ExecCommand(uintptr_t o, int16_t commandId) {
    uint8_t cmd[0x20] = {};
    *reinterpret_cast<int16_t*>(cmd) = commandId;
    return GameFn<intptr_t (*)(uintptr_t, void*)>(0x1403d6fc0)(o, cmd);
}

bool InIdleAction(uintptr_t o) {
    return Field<uint32_t>(o, obj::ActionHandle) == StaticHandle(ActionIdle);
}

void GroundJump(uintptr_t o) {
    GameFn<void (*)(void*, uintptr_t, char)>(0x140422630)(reinterpret_cast<void*>(VA(ActionIdle)), o, 0);
}

}  // namespace kh2
