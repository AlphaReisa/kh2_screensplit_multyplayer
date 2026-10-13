#include "common.h"
#include <cstdio>
#include <cctype>
#include "MinHook.h"

namespace kc {

LogFn Log = [](const char*, ...) {};
uintptr_t g_base = 0;
int g_frame = 0;
Config g_cfg;

bool CheckBytes(uintptr_t va, const char* sigHex) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(VA(va));
    int n = (int)strlen(sigHex) / 2;
    for (int i = 0; i < n; i++) {
        unsigned b;
        sscanf_s(sigHex + i * 2, "%2x", &b);
        if (p[i] != b) {
            Log("firma no coincide en %llx (byte %d)", (unsigned long long)va, i);
            return false;
        }
    }
    return true;
}

bool Hook(const char* name, uintptr_t va, const char* sigHex, void* detour, void** original) {
    if (!CheckBytes(va, sigHex)) {
        Log("hook %s: version del juego distinta", name);
        return false;
    }
    MH_STATUS s = MH_CreateHook(reinterpret_cast<void*>(VA(va)), detour, original);
    if (s != MH_OK) {
        Log("hook %s (%llx): MH_CreateHook = %d", name, (unsigned long long)va, s);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- keys
static bool g_prevDown[256];

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool KeyPressed(int vk) {
    bool down = KeyDown(vk);
    bool edge = down && !g_prevDown[vk & 0xFF];
    g_prevDown[vk & 0xFF] = down;
    return edge;
}

// ---------------------------------------------------------------- config
static int ParseKey(const char* s, int def) {
    if (!s || !*s) return def;
    if ((s[0] == 'F' || s[0] == 'f') && isdigit((unsigned char)s[1])) {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (!s[1] && isalnum((unsigned char)s[0])) return toupper((unsigned char)s[0]);
    return def;
}

void LoadConfig() {
    char path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LoadConfig), &self);
    GetModuleFileNameA(self, path, MAX_PATH);   // the loader copies the core next to the game exe
    if (char* slash = strrchr(path, '\\')) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "kh2coop.ini");

    char buf[32];
    GetPrivateProfileStringA("keys", "split", "F8", buf, sizeof(buf), path);
    g_cfg.keySplit = ParseKey(buf, VK_F8);
    GetPrivateProfileStringA("keys", "coop_mode", "F6", buf, sizeof(buf), path);
    g_cfg.keyCoopMode = ParseKey(buf, VK_F6);
    GetPrivateProfileStringA("keys", "repair", "F7", buf, sizeof(buf), path);
    g_cfg.keyRepair = ParseKey(buf, VK_F7);
    g_cfg.coopMode = GetPrivateProfileIntA("coop", "second_sora", 0, path) != 0;
}

}  // namespace kc
