#pragma once
#include "GameBridge.h"

namespace witchcraft {
// One-shot, input-free MC gravity test. Produces no GameControl and does not
// change the native adapter's scripted handoff or normal collision-ready flag.
class DiagnosticSettle {
public:
    enum class State { waiting, running, passed, failed };
    void Update(const GameFrame* game,const skycraft::proto::McState* mc,
        bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky);
    State Status() const { return state_; }
    const char* Reason() const { return reason_; }
private:
    State state_=State::waiting;
    GameFrame anchor_{};
    std::uint64_t stableAt_=0,startedAt_=0,groundAt_=0;
    bool haveAnchor_=false,groundSeen_=false,arrived_=false;
    std::uint32_t teleport_=0;
    const char* reason_="waiting for stationary player, delivered patch and live Minecraft";
};
}
