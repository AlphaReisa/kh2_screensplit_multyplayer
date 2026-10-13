// HUD. dk::GAUGE_PLAYER (vtable 0x1405b2100) shows the player stored at gauge+0x40 and reads its status
// (+0x5c0). When the second Sora is constructed the gauge gets re-bound to it; keep it on P1.
// HUD objects are driven by Hud2DTask (0x1401667d0)(task{+0x18 manager, +0x20 object}).
//
// NOTE (roadmap step 4): the P2 gauge will be a second real GAUGE_PLAYER built through the game's
// constructor and layout loading (cloning the object was tried and is not viable).
#include "modules.h"
#include "game.h"

using namespace kc;

namespace hud {

static const uintptr_t kGaugePlayerVt = 0x1405b2100;
using TaskFn = void (*)(intptr_t task);
static TaskFn o_Hud2DTask;

static void h_Hud2DTask(intptr_t task) {
    uintptr_t o = Field<uintptr_t>(task, 0x20);
    uintptr_t p1 = kh2::Player(), p2 = party::P2Sora();
    if (o && p1 && p2 && *reinterpret_cast<uintptr_t*>(o) == VA(kGaugePlayerVt) && Field<uintptr_t>(o, 0x40) == p2)
        Field<uintptr_t>(o, 0x40) = p1;
    o_Hud2DTask(task);
}

bool Install() {
    return Hook("Hud2DTask", 0x1401667d0, "48895c2408574883ec20488b5920488b", (void*)h_Hud2DTask, (void**)&o_Hud2DTask);
}

}  // namespace hud
