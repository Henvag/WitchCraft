#pragma once
#include "DiagnosticSettle.h"
namespace witchcraft {
// Minecraft owns physics; Witcher's hidden actor follows horizontally during jumps.
class PlaySession {
public:
    void Update(const GameFrame*,const skycraft::proto::McState*,bool alive,bool focused,
        bool consumed,std::uint64_t now,skycraft::proto::SkyState&,std::uint8_t keys,
        bool stop,bool inputReady,bool hidden,bool cameraOwned=false);
    bool Active() const {return active_;}
    void Resume() {
        if(active_) return;
        const auto sequence=sequence_;
        *this=PlaySession{};sequence_=sequence;
        reason_="F8 resume requested; waiting for a fresh grounded handoff";
    }
    std::uint8_t Keys() const {return keys_;}
    const char* Reason() const {return reason_;}
    double FollowLag() const {return followLag_;}
    bool GroundFollow() const {return active_;}
    void PilotFeet(const skycraft::proto::McState& mc,double* out) const {
        out[0]=mc.curX;out[1]=nativeGround_;out[2]=mc.curZ;
    }
    void Stop(const char* reason) {if(finished_) return;active_=false;finished_=true;keys_=0;reason_=reason;}
private:
    DiagnosticSettle settle_;
    GameFrame anchor_{};
    std::uint32_t sequence_=0;
    std::uint64_t frame_=0,freshAt_=0,jumpAt_=0,airAt_=0,catchUpAt_=0,coverageAt_=0;
    double nativeGround_=0,jumpGround_=0,peak_=0;
    double followLag_=0;
    bool catchingUp_=false;
    bool waitingCoverage_=false;
    bool active_=false,finished_=false,started_=false,spaceDown_=false,jumping_=false,tookOff_=false,air_=false;
    bool holdPhysics_=false;
    std::uint8_t keys_=0;
    const char* reason_="waiting for collision and stationary player";
};
}
