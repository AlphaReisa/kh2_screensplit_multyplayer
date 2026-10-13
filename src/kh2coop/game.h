// KH2-Coop — what we know about the engine (KH2FM Steam). Addresses are Ghidra VAs; see research/NOTES.md.
#pragma once
#include "common.h"

namespace kh2 {

// ---------------------------------------------------------------- globals
constexpr uintptr_t PlayerPtr      = 0x142a105d0;  // YS::PLAYER* g_Player (set by the PLAYER ctor)
constexpr uintptr_t SoraPtr        = 0x142ae9a28;  // YS::SORA*   g_Sora   (set by the SORA ctor)
constexpr uintptr_t CameraTarget   = 0x140718cb0;  // object followed by the game camera
constexpr uintptr_t PlayerControl  = 0x142a11100;  // player controller {obj, pad*, ...}
constexpr uintptr_t ObjectListHead = 0x142a171c8;  // first active object; next = handle at obj+0xa90
constexpr uintptr_t DataStart      = 0x140713000;  // .data (static object pool lives here)
constexpr uintptr_t DataEnd        = 0x142b82000;

// ---------------------------------------------------------------- type objects / vtables
constexpr uintptr_t SoraType       = 0x140750300;  // YS::PLAYER::VTABLE<YS::SORA>
constexpr uintptr_t FriendType     = 0x14071d7c0;  // kn::Friend::VTABLE<kn::Friend>
constexpr uintptr_t GoofyBrainVt   = 0x1405b42d8;  // kn::Goofy (friend AI personality)
constexpr uintptr_t ActionIdle     = 0x140750950;  // YS::ACTION_IDLE singleton (ground locomotion)

// ---------------------------------------------------------------- object fields (YS::OBJ and up)
namespace obj {
constexpr size_t TypeHandle   = 0x000;  // u32 handle -> type object
constexpr size_t ActionHandle = 0x00c;  // u32 handle -> current YS::ACTION_*
constexpr size_t MoveVec      = 0x010;  // vec4 desired move (x, 0, z, speed)
constexpr size_t Scale        = 0x030;  // vec4 (1,1,1,1) for live models
constexpr size_t Pos          = 0x070;  // vec4 world position (copied from TruePos each frame)
constexpr size_t RunSpeed     = 0x12c;  // float, w of MoveVec when running
constexpr size_t Status       = 0x5c0;  // battle status slot (HP +0, max +4, MP +0x180, max +0x184)
constexpr size_t Yaw          = 0x68c;  // float, facing
constexpr size_t ListNext     = 0xa90;  // u32 handle of the next active object
constexpr size_t FriendBrain  = 0xdf8;  // kn::Friend: AI personality object
constexpr size_t FriendBehave = 0xe00;  // kn::Friend: current behaviour routine
}  // namespace obj

// ---------------------------------------------------------------- camera / projection banks
constexpr uintptr_t CamIndex   = 0x140ac1838;  // active camera index (0/1)
constexpr uintptr_t CamBank    = 0x140ac1020;  // 2 x 0x80: +0x08 view matrix, +0x48 eye, +0x58 target
constexpr uintptr_t ProjBank   = 0x140ac1840;  // 2 x 0x184 projection structs
constexpr uintptr_t FadeTask   = 0x140abcca0;  // current camera-crossfade task (0 = none)
constexpr uintptr_t FadeAlpha  = 0x140abcc50;

// ---------------------------------------------------------------- pads (PS2 heritage: 2 pads)
constexpr uintptr_t Pad0     = 0x140bf31a0;  // GetPad(i) = Pad0 + i*PadSize
constexpr size_t    PadSize  = 0x68;          // +0 cur, +8 pressed, +0x10 released, +0x18 repeat,
                                              // +0x30 stick (x, y(-fwd), 0, mag), +0x48 raw*, +0x50 flags

// ---------------------------------------------------------------- engine helpers
uintptr_t HandleToPtr(uint32_t h);
uint32_t  PtrToHandle(const void* p);
uintptr_t Player();
bool      IsType(uintptr_t obj, uintptr_t typeObj);
bool      InObjectList(uintptr_t obj);          // linked in the active object list (= alive)
bool      IsLiveSora(uintptr_t obj);
bool      IsLiveGoofy(uintptr_t obj);
int       FindPoolObjects(uintptr_t typeObj, uintptr_t* out, int max, uintptr_t skip = 0);
inline float* Pos(uintptr_t o) { return reinterpret_cast<float*>(o + obj::Pos); }

// battle command / action helpers
intptr_t ExecCommand(uintptr_t obj, int16_t commandId);   // 0x1403d6fc0, 1 = Attack
void     GroundJump(uintptr_t obj);                       // ACTION_IDLE slot 9 (jump from ground)
bool     InIdleAction(uintptr_t obj);

}  // namespace kh2
