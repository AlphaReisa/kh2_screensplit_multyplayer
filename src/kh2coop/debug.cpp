// Diagnostics, compiled only in debug builds (build.bat debug -> /DKH2COOP_DEBUG).
//   F3  hardware watchpoints on camera slot 0 (view row 0, translation, eye, +0x68) for ~6 frames
//   F2  hardware watchpoints on the HUD gauge's player pointer
//   F11 hardware watchpoints on writes to Goofy's move vector (1.5 s)
//   F5  trace every MapDraw call for one frame (pass index, caller, VP translation)
//   F4  cycle -1/0/1: feed MapDraw a broken view matrix in that pass
// Each watchpoint hit is logged once per (rip, caller, camera index, register) with a 4-frame call stack.
#include "modules.h"
#include "game.h"
#include <intrin.h>

using namespace kc;

#ifdef KH2COOP_DEBUG

namespace debugtools {

// ---------------------------------------------------------------- hardware watchpoints
struct Watch { uintptr_t addr; int rw; int len; };   // rw: 1 write, 3 read/write; len: 1,2,4,8
static Watch g_watch[4];
static int g_watchCount;
static int g_watchMs;
static DWORD g_gameThread;
static volatile LONG g_armed = 0;
static PVOID g_veh;
struct Hit { uintptr_t rip, stack[4]; int idx, dr, count; };
static Hit g_hits[256];
static volatile LONG g_hitCount;

static LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    if ((c->Dr6 & 0xF) == 0) return EXCEPTION_CONTINUE_SEARCH;
    int dr = (c->Dr6 & 1) ? 0 : (c->Dr6 & 2) ? 1 : (c->Dr6 & 4) ? 2 : 3;
    uintptr_t rip = (uintptr_t)c->Rip;
    uintptr_t stk[4] = {};
    void* frames[24];
    USHORT nf = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    int k = 0;
    bool pastFault = false;
    for (USHORT i = 0; i < nf && k < 4; i++) {
        uintptr_t f = (uintptr_t)frames[i];
        if (f < g_base || f >= g_base + 0x2c00000) continue;   // game exe frames only
        if (!pastFault) { pastFault = true; if (f == rip) continue; }
        stk[k++] = f;
    }
    int idx = Global<int32_t>(kh2::CamIndex);
    LONG n = g_hitCount;
    for (LONG i = 0; i < n; i++)
        if (g_hits[i].rip == rip && g_hits[i].stack[0] == stk[0] && g_hits[i].idx == idx && g_hits[i].dr == dr) {
            g_hits[i].count++;
            c->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    if (n < 256) { g_hits[n] = {rip, {stk[0], stk[1], stk[2], stk[3]}, idx, dr, 1}; g_hitCount = n + 1; }
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void ApplyWatch(bool on) {
    HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, g_gameThread);
    if (!t) { Log("OpenThread fallo %lu", GetLastError()); return; }
    SuspendThread(t);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(t, &c);
    DWORD64* dr[4] = {&c.Dr0, &c.Dr1, &c.Dr2, &c.Dr3};
    c.Dr7 = 0;
    for (int i = 0; i < 4; i++) {
        *dr[i] = on && i < g_watchCount ? g_watch[i].addr : 0;
        if (!on || i >= g_watchCount) continue;
        int lenBits = g_watch[i].len == 1 ? 0 : g_watch[i].len == 2 ? 1 : g_watch[i].len == 8 ? 2 : 3;
        c.Dr7 |= (1ull << (i * 2)) | ((DWORD64)(g_watch[i].rw | (lenBits << 2)) << (16 + i * 4));
    }
    SetThreadContext(t, &c);
    ResumeThread(t);
    CloseHandle(t);
}

static DWORD WINAPI WatchThread(LPVOID) {
    g_hitCount = 0;
    ApplyWatch(true);
    Sleep(g_watchMs);
    ApplyWatch(false);
    LONG n = g_hitCount;
    Log("watch: %ld accesos distintos", n);
    for (LONG i = 0; i < n; i++)
        Log("  dr%d idx=%d rip=%llx x%d  <- %llx <- %llx <- %llx <- %llx", g_hits[i].dr, g_hits[i].idx,
            Rel(g_hits[i].rip), g_hits[i].count, Rel(g_hits[i].stack[0]), Rel(g_hits[i].stack[1]),
            Rel(g_hits[i].stack[2]), Rel(g_hits[i].stack[3]));
    g_armed = 0;
    return 0;
}

static void StartWatch(const char* what, std::initializer_list<Watch> w, int ms) {
    if (InterlockedCompareExchange(&g_armed, 1, 0) != 0) return;
    g_watchCount = 0;
    for (const Watch& x : w) if (g_watchCount < 4 && x.addr) g_watch[g_watchCount++] = x;
    g_watchMs = ms;
    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, WatchHandler);
    Log("watchpoints: %s", what);
    CloseHandle(CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr));
}

// ---------------------------------------------------------------- MapDraw trace / break
using MapDrawFn = intptr_t (*)(intptr_t part, uint64_t a2, uint64_t a3, uint32_t* a4, uint32_t a5);
static MapDrawFn o_MapDraw;
static volatile int g_traceFrames = 0;
static volatile int g_breakIdx = -1;
static uint8_t g_vpCopy[0x260];

static intptr_t h_MapDraw(intptr_t part, uint64_t a2, uint64_t a3, uint32_t* a4, uint32_t a5) {
    int idx = Global<int32_t>(kh2::CamIndex);
    if (g_traceFrames > 0) {
        const float* vp = reinterpret_cast<const float*>(a2);
        Log("  MapDraw idx=%d ret=%llx part=%llx vp[12..15]=%.1f %.1f %.1f %.1f", idx,
            Rel((uintptr_t)_ReturnAddress()), Rel(part), vp[12], vp[13], vp[14], vp[15]);
    }
    if (g_breakIdx >= 0 && idx == g_breakIdx) {
        memcpy(g_vpCopy, reinterpret_cast<void*>(a2), 0x248);
        float* v = reinterpret_cast<float*>(g_vpCopy + 0x1c4);   // view matrix copy
        v[12] += 3000.0f;
        v[13] += 3000.0f;
        a2 = (uint64_t)g_vpCopy;
    }
    return o_MapDraw(part, a2, a3, a4, a5);
}

bool Install() {
    return Hook("MapDraw", 0x1401a5180, "40555356574154415541564157488dac", (void*)h_MapDraw, (void**)&o_MapDraw);
}

void Frame() {
    g_gameThread = GetCurrentThreadId();
    if (g_traceFrames > 0) g_traceFrames--;
    if (KeyPressed(VK_F5)) { g_traceFrames = 2; Log("traza MapDraw de un frame"); }
    if (KeyPressed(VK_F4)) { g_breakIdx = g_breakIdx == 1 ? -1 : g_breakIdx + 1; Log("romper MapDraw en pase %d", g_breakIdx); }
    if (KeyPressed(VK_F3)) {
        uintptr_t s = VA(kh2::CamBank);
        StartWatch("camara ranura 0", {{s + 0x08, 3, 8}, {s + 0x38, 3, 8}, {s + 0x48, 3, 8}, {s + 0x68, 3, 8}}, 100);
    }
    if (KeyPressed(VK_F2)) {
        uintptr_t g = VA(0x142578b20);
        StartWatch("gauge del HUD", {{g + 0x40, 3, 8}, {g + 0x28, 3, 8}}, 100);
    }
    if (KeyPressed(VK_F11)) {
        friendctl::Refresh();
        if (uintptr_t g = friendctl::Goofy())
            StartWatch("vector de movimiento de Goofy", {{g + 0x10, 1, 4}, {g + 0x1c, 1, 4}}, 1500);
    }
}

void Shutdown() {
    while (g_armed) Sleep(10);
    if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = nullptr; }
}

}  // namespace debugtools

#else

namespace debugtools {
bool Install() { return true; }
void Frame() {}
void Shutdown() {}
}  // namespace debugtools

#endif
