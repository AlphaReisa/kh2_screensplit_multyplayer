// KH2-Coop — module interfaces. Each module installs its own hooks and gets a per-frame tick from the
// core (on the game thread, at the start of the render task list, before the engine's RenderPrep).
#pragma once
#include "common.h"

// ---------------------------------------------------------------- split screen (split.cpp)
namespace split {
bool Install();
void Frame();          // F8 toggle, keep the dual-camera mode alive
void Shutdown();       // back to single camera (called from the loader thread, hooks still on)
void ShutdownAfterHooks();  // restore projection matrices once hooks are disabled
bool Running();        // dual-camera render mode active
bool Wanted();
bool P2CameraForward(float& fx, float& fz);   // camera 2 view direction in XZ (unit); false if not set up
}

// ---------------------------------------------------------------- player-2 input (input.cpp)
namespace input {
struct P2State {
    float stickX = 0, stickY = 0;   // -1..1, y = forward
    float camX = 0;                 // right stick / camera keys, -1..1 (positive = orbit right)
    float camY = 0;                 // right stick vertical, -1..1 (positive = up)
    bool  stickActive = false;
    WORD  buttons = 0;              // XInput layout: A 0x1000, B 0x2000, X 0x4000, Y 0x8000, Back 0x20
    WORD  pressed = 0;              // pressed on the last poll
    WORD  pressedAccum = 0;         // pressed since last consumed (see TakePressed)
};
bool Install();
void Frame();                       // poll XInput user 1 + keyboard stand-ins
const P2State& P2();
bool Joined();             // a controller has joined as player 2
WORD TakePressed();                 // returns and clears pressedAccum
void SwapPads();                    // exchange engine pad 0 and pad 1 (used around the P2 Sora update)
void Shutdown();
}

// ---------------------------------------------------------------- second Sora (party.cpp)
namespace party {
bool Install();
void Frame();
uintptr_t P2Sora();                 // live second YS::SORA, or 0
}

// ---------------------------------------------------------------- Goofy driven by player 2 (friend.cpp)
namespace friendctl {
bool Install();
void Frame();
uintptr_t Goofy();                  // live Goofy (validated every frame), or 0
bool Controlled();                  // player 2 currently owns Goofy (AI off)
void Refresh();                     // re-validate / re-scan Goofy
}

// ---------------------------------------------------------------- HUD (hud.cpp)
namespace hud {
bool Install();
void Frame();   // player 2's gauge: create with the HUD, bind to the second Sora; split placement
void Shutdown();
}

// ---------------------------------------------------------------- diagnostics (debug.cpp, debug builds only)
namespace debugtools {
bool Install();
void Frame();
void Shutdown();
}
