// Diagnostics, compiled only in debug builds (build.bat debug -> /DKH2COOP_DEBUG).
//   F3  hardware watchpoints on camera slot 0 (view row 0, translation, eye, +0x68) for ~6 frames
//   F2  hardware watchpoints on the HUD gauge's player pointer
//   F9  hardware watchpoints listed in kh2coop_watch.txt (written by src/tools/watch.py)
//   F11 hardware watchpoints on writes to Goofy's move vector (1.5 s)
//   F5  trace every MapDraw call for one frame (pass index, caller, VP translation)
//   F4  cycle -1/0/1: feed MapDraw a broken view matrix in that pass
// Each watchpoint hit is logged once per (rip, caller, camera index, register) with a 4-frame call stack.
#include "modules.h"
#include "game.h"
#include <intrin.h>
#include <cstdio>
#include <cstdlib>

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

// Generic watch: kh2coop_watch.txt next to the game exe, up to 4 lines "<live addr hex> <r|w> <len> [ms]"
// (written by src/tools/watch.py).
static void WatchFromFile() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\')) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "kh2coop_watch.txt");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") || !f) { Log("watch: no existe %s", path); return; }
    Watch w[4] = {};
    int n = 0, ms = 300;
    char line[128];
    while (n < 4 && fgets(line, sizeof(line), f)) {
        unsigned long long a; char rw; int len, t = 0;
        int got = sscanf_s(line, "%llx %c %d %d", &a, &rw, 1, &len, &t);
        if (got < 3) continue;
        w[n++] = {(uintptr_t)a, rw == 'w' ? 1 : 3, len};
        if (got == 4 && t > 0) ms = t;
    }
    fclose(f);
    if (InterlockedCompareExchange(&g_armed, 1, 0) != 0) return;
    g_watchCount = n;
    memcpy(g_watch, w, sizeof(w));
    g_watchMs = ms;
    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, WatchHandler);
    Log("watchpoints desde archivo: %d direcciones, %d ms", n, ms);
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

// ---------------------------------------------------------------- first-chance crash log
// The game's own crash handler can overflow its stack and lose the original fault: log it first.
static PVOID g_crashVeh;
static LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* e) {
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG count = 0;
    if (InterlockedIncrement(&count) > 4) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    Log("FALLO %08lx en %llx (rip %llx) acceso %llx %llx", code, Rel(c->Rip), (unsigned long long)c->Rip,
        (unsigned long long)e->ExceptionRecord->ExceptionInformation[0], (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
    Log("  rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx r8 %llx", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->R8);
    // return addresses into the game / the core on the stack
    const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(c->Rsp);
    int shown = 0;
    for (int i = 0; i < 256 && shown < 20; i++) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(sp + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
        uintptr_t v = sp[i];
        if (v >= g_base && v < g_base + 0x700000) { Log("  pila+%x: %llx", i * 8, Rel(v)); shown++; }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------- dead effect tracking
// Crash under investigation: a script asks for the position of an effect instance (EffectPos 0x1402be970 ->
// instance+0x30 node) that was already killed (EffectKill 0x1402be8a0 clears +0x30). Every kill is recorded
// with its call stack; a position request on a killed instance logs who killed it and gets a zero vector.
struct Kill { uintptr_t inst; uintptr_t stack[5]; int frame; bool p2Update; };
static const size_t kKillSlots = 1 << 16;   // last kill of each instance (open addressing, keyed by address)
static Kill* g_kills;
static Kill* KillSlot(uintptr_t inst, bool create) {
    if (!g_kills) g_kills = static_cast<Kill*>(VirtualAlloc(nullptr, sizeof(Kill) * kKillSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_kills) return nullptr;
    size_t i = (inst >> 4) * 0x9E3779B97F4A7C15ull >> 48;
    for (size_t n = 0; n < 64; n++, i = (i + 1) & (kKillSlots - 1)) {
        if (g_kills[i].inst == inst) return &g_kills[i];
        if (!g_kills[i].inst) return create ? &g_kills[i] : nullptr;
    }
    return create ? &g_kills[i] : nullptr;
}
using EffectFn = void (*)(uintptr_t inst);
using EffectPosFn = void* (*)(uintptr_t inst);
static EffectFn o_EffectKill;
static EffectPosFn o_EffectPos;
static float g_zero[4];

static void h_EffectKill(uintptr_t inst) {
    if (Field<uintptr_t>(inst, 0x30)) {
        Kill* slot = KillSlot(inst, true);
        if (!slot) { o_EffectKill(inst); return; }
        Kill& k = *slot;
        k = {};
        k.inst = inst;
        k.frame = g_frame;
        k.p2Update = party::InP2Update();
        void* frames[16];
        USHORT n = RtlCaptureStackBackTrace(1, 16, frames, nullptr);
        for (USHORT i = 0, j = 0; i < n && j < 5; i++) {
            uintptr_t f = (uintptr_t)frames[i];
            if (f >= g_base && f < g_base + 0x2c00000) k.stack[j++] = f;
        }
    }
    o_EffectKill(inst);
}

static void* h_EffectPos(uintptr_t inst) {
    uintptr_t node = inst ? Field<uintptr_t>(inst, 0x30) : 1;
    if (node == 0 || node == 0xefaccafeefaccafeull) {   // killed, or its memory already freed (0xEFACCAFE fill)
        static uintptr_t reported[64];
        static int nReported;
        bool seen = false;
        for (int i = 0; i < nReported; i++) seen |= reported[i] == inst;
        if (!seen) {
            if (nReported < 64) reported[nReported++] = inst;
            void* frames[16];
            uintptr_t st[5] = {};
            USHORT n = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
            for (USHORT i = 0, j = 0; i < n && j < 5; i++) {
                uintptr_t f = (uintptr_t)frames[i];
                if (f >= g_base && f < g_base + 0x2c00000) st[j++] = f;
            }
            Log("EFECTO MUERTO consultado (%s): %llx (frame %d) <- %llx <- %llx <- %llx <- %llx <- %llx",
                node ? "memoria liberada" : "matado", Rel(inst), g_frame, Rel(st[0]), Rel(st[1]), Rel(st[2]), Rel(st[3]), Rel(st[4]));
            bool found = false;
            if (const Kill* k = KillSlot(inst, false)) {
                found = true;
                Log("  matado en frame %d (%s): %llx <- %llx <- %llx <- %llx <- %llx", k->frame,
                    k->p2Update ? "update de P2" : "fuera del update de P2", Rel(k->stack[0]),
                    Rel(k->stack[1]), Rel(k->stack[2]), Rel(k->stack[3]), Rel(k->stack[4]));
            }
            if (!found) Log("  (sin registro de muerte: matado antes de cargar el nucleo o por otra via)");
            Log("  jugador %llx, Sora P2 %llx", Rel(kh2::Player()), Rel(party::P2Sora()));
            // who still refers to it: pointers and engine handles in the game's .data (static object pool included)
            uint32_t h = kh2::PtrToHandle(reinterpret_cast<void*>(inst));
            int hits = 0;
            for (uintptr_t q = VA(kh2::DataStart); q < VA(kh2::DataEnd) && hits < 24; q += 4) {
                if (*reinterpret_cast<uint32_t*>(q) == h ||
                    ((q & 7) == 0 && *reinterpret_cast<uintptr_t*>(q) == inst)) {
                    Log("  referencia en %llx (%s)", Rel(q), *reinterpret_cast<uint32_t*>(q) == h ? "handle" : "puntero");
                    hits++;
                }
            }
        }
        return g_zero;
    }
    return o_EffectPos(inst);
}

// An effect instance whose start fails (EffectStart 0x1402bec80 -> 0 leaves +0x30 null) is what scripts later
// query as "dead": log those failures with the effect data they were started with and who started them.
using EffectStartFn = int (*)(uintptr_t inst, uintptr_t arg);
static EffectStartFn o_EffectStart;
static int h_EffectStart(uintptr_t inst, uintptr_t arg) {
    int r = o_EffectStart(inst, arg);
    if (!r && Field<int32_t>(inst, 8) != 4) {
        static int n;
        if (n++ < 40) {
            void* frames[16];
            uintptr_t st[6] = {};
            USHORT c = RtlCaptureStackBackTrace(1, 16, frames, nullptr);
            for (USHORT i = 0, j = 0; i < c && j < 6; i++) {
                uintptr_t f = (uintptr_t)frames[i];
                if (f >= g_base && f < g_base + 0x2c00000) st[j++] = f;
            }
            Log("EFECTO NO ARRANCA %llx: datos %llx idx %d tipo %d (%s) <- %llx <- %llx <- %llx <- %llx <- %llx <- %llx",
                Rel(inst), Rel(Field<uintptr_t>(inst, 0x20)), Field<int32_t>(inst, 0x28), Field<int32_t>(inst, 0x18),
                party::InP2Update() ? "update P2" : "fuera", Rel(st[0]), Rel(st[1]), Rel(st[2]), Rel(st[3]), Rel(st[4]), Rel(st[5]));
        }
    }
    return r;
}

bool Install() {
    g_crashVeh = AddVectoredExceptionHandler(1, CrashLogger);
    return Hook("MapDraw", 0x1401a5180, "40555356574154415541564157488dac", (void*)h_MapDraw, (void**)&o_MapDraw)
        && Hook("EffectKill", 0x1402be8a0, "40534883ec2081490c01000100488bd9", (void*)h_EffectKill, (void**)&o_EffectKill)
        && Hook("EffectStart", 0x1402bec80, "40534883ec4083790804488bd90f2974", (void*)h_EffectStart, (void**)&o_EffectStart)
        && Hook("EffectPos", 0x1402be970, "488b4930e967460000cccccccccccccc", (void*)h_EffectPos, (void**)&o_EffectPos);
}

// In-process XInput view (Steam Input may add virtual devices that external tools don't see):
// logs which XInput users have a deflected left stick, whenever that set changes.
static void LogXInputUsers() {
    using GetStateFn = DWORD(WINAPI*)(DWORD, void*);
    static GetStateFn get = nullptr;
    if (!get) { HMODULE m = LoadLibraryA("xinput1_4.dll"); get = m ? (GetStateFn)GetProcAddress(m, "XInputGetState") : nullptr; }
    if (!get) return;
    struct XS { DWORD pk; WORD b; BYTE lt, rt; SHORT lx, ly, rx, ry; };
    static int lastMask = -1, lastConn = -1;
    int mask = 0, conn = 0;
    for (DWORD u = 0; u < 4; u++) {
        XS st{};
        if (get(u, &st) != 0) continue;
        conn |= 1 << u;
        if (abs(st.lx) > 12000 || abs(st.ly) > 12000 || st.b) mask |= 1 << u;
    }
    if (mask != lastMask || conn != lastConn) {
        Log("xinput en el juego: conectados %x, con entrada %x", conn, mask);
        lastMask = mask; lastConn = conn;
    }
}

// Diagnosing: the second Sora's MP refilling by itself. When it goes up, watch who writes it (once).
static void WatchP2Mp() {
    static int last = -1;
    static bool done = false;
    uintptr_t p2 = party::P2Sora();
    uintptr_t slot = p2 ? Field<uintptr_t>(p2, kh2::obj::Status) : 0;
    if (!slot) { last = -1; return; }
    int mp = Field<int32_t>(slot, 0x180);
    if (last >= 0 && mp > last && !done) {
        done = true;
        Log("PM de Sora P2 sube %d -> %d (ficha %llx)", last, mp, Rel(slot));
        StartWatch("PM de Sora P2", {{slot + 0x180, 1, 4}}, 3000);
    }
    last = mp;
}

void Frame() {
    g_gameThread = GetCurrentThreadId();
    WatchP2Mp();
    LogXInputUsers();
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
    if (KeyPressed(VK_F9)) WatchFromFile();
    if (KeyPressed(VK_F11)) {
        friendctl::Refresh();
        if (uintptr_t g = friendctl::Goofy())
            StartWatch("vector de movimiento de Goofy", {{g + 0x10, 1, 4}, {g + 0x1c, 1, 4}}, 1500);
    }
}

void Shutdown() {
    if (g_crashVeh) { RemoveVectoredExceptionHandler(g_crashVeh); g_crashVeh = nullptr; }
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
