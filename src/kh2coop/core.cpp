// KH2-Coop core: hot-reloadable DLL loaded by the dinput8.dll loader (see loader.cpp).
// Wires the modules together: each installs its hooks; RenderPrep (start of the render task list, once
// per frame on the game thread) drives their per-frame ticks.
#include "modules.h"
#include "game.h"
#include "MinHook.h"

using namespace kc;

#define KH2COOP_VERSION "0.37"

using VoidFn = void (*)();
static VoidFn o_RenderPrep;

static void h_RenderPrep() {
    g_frame++;
    input::Frame();       // poll player 2
    party::Frame();       // second Sora: discovery, repair, live swap on join/leave
    friendctl::Frame();   // Goofy under player 2 (when there is no second Sora)
    hud::Frame();         // player 2 gauge
    debugtools::Frame();
    split::Frame();       // split-screen toggle / keep-alive (must run before the engine's RenderPrep)
    o_RenderPrep();
}

extern "C" __declspec(dllexport) bool CoreInit(LogFn log) {
    Log = log;
    g_base = (uintptr_t)GetModuleHandleW(nullptr);
    LoadConfig();
#ifdef KH2COOP_DEBUG
    Log("KH2-Coop core v" KH2COOP_VERSION " (debug)  base=%llx", (unsigned long long)g_base);
#else
    Log("KH2-Coop core v" KH2COOP_VERSION "  base=%llx", (unsigned long long)g_base);
#endif
    if (MH_Initialize() != MH_OK) { Log("MH_Initialize fallo"); return false; }
    bool ok = Hook("RenderPrep", 0x14016fe70, "40534883ec20488b0dc3d59400e8ee80", (void*)h_RenderPrep, (void**)&o_RenderPrep)
           && CheckBytes(0x1404ad270, "85c9750333c0c3e974010000cccccccc")   // HandleToPtr (engine codec)
           && CheckBytes(0x1404ad240, "40534883ec20488bd94885c9750833c0")   // PtrToHandle
           && split::Install()
           && input::Install()
           && party::Install()
           && friendctl::Install()
           && hud::Install()
           && debugtools::Install();
    if (!ok || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("no se activan los hooks (version del juego distinta o fallo de MinHook)");
        MH_Uninitialize();
        return false;
    }
    Log("listo. pantalla dividida: tecla %d, jugador 2 como Sora: %d", g_cfg.keySplit, g_cfg.secondSora);
    return true;
}

extern "C" __declspec(dllexport) void CoreShutdown() {
    split::Shutdown();            // ends the dual-camera mode on the game thread
    input::Shutdown();
    hud::Shutdown();
    MH_DisableHook(MH_ALL_HOOKS);
    debugtools::Shutdown();
    split::ShutdownAfterHooks();
    Sleep(100);                   // let in-flight detours return
    MH_Uninitialize();
    Log("core apagado");
}
