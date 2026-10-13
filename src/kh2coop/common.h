// KH2-Coop — shared infrastructure: logging, address translation, hook installation, keys, config.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <initializer_list>

namespace kc {

// ---------------------------------------------------------------- logging (provided by the loader)
using LogFn = void (*)(const char*, ...);
extern LogFn Log;

// ---------------------------------------------------------------- addresses
// All game addresses in this project are Ghidra VAs of the exe (image base 0x140000000).
extern uintptr_t g_base;
inline uintptr_t VA(uintptr_t ghidra) { return g_base + (ghidra - 0x140000000ull); }
inline unsigned long long Rel(uintptr_t live) { return live ? live - g_base + 0x140000000ull : 0; }
template <class T> inline T& Global(uintptr_t ghidra) { return *reinterpret_cast<T*>(VA(ghidra)); }
template <class T> inline T& Field(uintptr_t obj, size_t off) { return *reinterpret_cast<T*>(obj + off); }
template <class Fn> inline Fn GameFn(uintptr_t ghidra) { return reinterpret_cast<Fn>(VA(ghidra)); }

// Frame counter, advanced once per rendered frame by the core.
extern int g_frame;

// ---------------------------------------------------------------- hooks
// Every hook is declared next to the expected first bytes of the target function, so a game update
// that moves code makes the core refuse to install instead of patching the wrong place.
bool Hook(const char* name, uintptr_t va, const char* sigHex, void* detour, void** original);
bool CheckBytes(uintptr_t va, const char* sigHex);

// ---------------------------------------------------------------- keys
// Rising-edge detection for keyboard keys (polled once per frame from the game thread).
bool KeyPressed(int vk);
bool KeyDown(int vk);

// ---------------------------------------------------------------- config (kh2coop.ini next to the game exe)
struct Config {
    int  keySplit    = VK_F8;   // toggle split screen
    int  keyRepair   = VK_F7;   // re-bind globals for a P2 Sora that existed before a core reload
    bool secondSora  = true;    // player 2 plays a second Sora (false: player 2 controls Goofy)
};
extern Config g_cfg;
void LoadConfig();

}  // namespace kc
