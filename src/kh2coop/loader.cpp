// KH2-Coop loader — proxy DINPUT8.dll. Forwards DirectInput8Create and hot-reloads kh2coop_core.dll
// whenever the file changes (copy-on-load, so the build output is never locked).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <share.h>

static FILE* g_log;
static CRITICAL_SECTION g_logLock;

extern "C" void LoaderLog(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); fputc('\n', g_log); fflush(g_log);
    va_end(ap);
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------- DirectInput8 forwarding
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_realDI8;

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, LPUNKNOWN u) {
    if (!g_realDI8) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat_s(path, "\\dinput8.dll");
        HMODULE m = LoadLibraryA(path);
        g_realDI8 = m ? (DirectInput8Create_t)GetProcAddress(m, "DirectInput8Create") : nullptr;
        if (!g_realDI8) return E_FAIL;
    }
    return g_realDI8(h, v, r, o, u);
}

// ---------------------------------------------------------------- core hot reload
using LogFn = void(*)(const char*, ...);
using CoreInitFn = bool(*)(LogFn);
using CoreShutdownFn = void(*)();

static char g_gameDir[MAX_PATH];
static char g_corePath[MAX_PATH];
static HMODULE g_core;
static CoreShutdownFn g_coreShutdown;

static FILETIME FileTime(const char* p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &d)) return FILETIME{};
    return d.ftLastWriteTime;
}

static void UnloadCore() {
    if (!g_core) return;
    if (g_coreShutdown) g_coreShutdown();
    FreeLibrary(g_core);
    g_core = nullptr; g_coreShutdown = nullptr;
    LoaderLog("[loader] core descargado");
}

static void LoadCore() {
    static int gen = 0;
    char copy[MAX_PATH];
    sprintf_s(copy, "%s\\kh2coop_core_%d.dll", g_gameDir, gen++ % 4);
    if (!CopyFileA(g_corePath, copy, FALSE)) { LoaderLog("[loader] no puedo copiar %s (%lu)", g_corePath, GetLastError()); return; }
    HMODULE m = LoadLibraryA(copy);
    if (!m) { LoaderLog("[loader] LoadLibrary %s fallo (%lu)", copy, GetLastError()); return; }
    auto init = (CoreInitFn)GetProcAddress(m, "CoreInit");
    auto shut = (CoreShutdownFn)GetProcAddress(m, "CoreShutdown");
    if (!init || !shut || !init(LoaderLog)) { LoaderLog("[loader] CoreInit fallo"); FreeLibrary(m); return; }
    g_core = m; g_coreShutdown = shut;
    LoaderLog("[loader] core cargado: %s", copy);
}

static DWORD WINAPI WatchThread(LPVOID) {
    Sleep(2000);
    FILETIME last{};
    for (;;) {
        FILETIME now = FileTime(g_corePath);
        if ((now.dwLowDateTime || now.dwHighDateTime) && CompareFileTime(&now, &last) != 0) {
            Sleep(500);                       // let the linker finish writing
            last = FileTime(g_corePath);
            UnloadCore();
            LoadCore();
        }
        Sleep(500);
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        InitializeCriticalSection(&g_logLock);
        GetModuleFileNameA(mod, g_gameDir, MAX_PATH);
        if (char* s = strrchr(g_gameDir, '\\')) *s = 0;
        char p[MAX_PATH];
        sprintf_s(p, "%s\\kh2coop.log", g_gameDir);
        g_log = _fsopen(p, "w", _SH_DENYNO);
        // core location: first line of kh2coop_core_path.txt, else next to this dll
        sprintf_s(g_corePath, "%s\\kh2coop_core.dll", g_gameDir);
        sprintf_s(p, "%s\\kh2coop_core_path.txt", g_gameDir);
        if (FILE* f = _fsopen(p, "r", _SH_DENYNO)) {
            if (fgets(g_corePath, MAX_PATH, f)) g_corePath[strcspn(g_corePath, "\r\n")] = 0;
            fclose(f);
        }
        LoaderLog("[loader] KH2-Coop loader, core = %s", g_corePath);
        CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
