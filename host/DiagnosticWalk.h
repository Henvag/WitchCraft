#pragma once
#include "DiagnosticSettle.h"

namespace witchcraft {
class DiagnosticWalk {
public:
    explicit DiagnosticWalk(bool followNative=false,bool manual=false):followNative_(followNative),manual_(manual) {}
    enum class State { settling, walking, braking, passed, failed, ready, cancelled, stopped };
    void Update(const GameFrame* game,const skycraft::proto::McState* mc,
        bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky,
        bool forward=false,bool stop=false,bool inputReady=true,bool nativeHidden=true);
    State Status() const { return state_; }
    const char* Reason() const { return reason_; }
    bool ForwardDown() const { return state_==State::walking; }
    bool Active() const {return state_==State::ready||state_==State::walking||state_==State::braking||state_==State::stopped;}
    double Distance() const { return distance_; }
    void Stop(const char* why) {state_=State::failed;reason_=why;haveWalk_=true;}
private:
    DiagnosticSettle settle_;
    State state_=State::settling;
    GameFrame anchor_{};
    double start_[3]{},last_[3]{},distance_=0;
    float yaw_=0;
    std::uint32_t teleport_=0;
    std::uint32_t settleSequence_=0;
    std::uint64_t startedAt_=0,brakeAt_=0,freshAt_=0,frame_=0,airAt_=0;
    bool airborne_=false,haveWalk_=false;
    bool followNative_=false;
    bool manual_=false;
    const char* reason_="waiting for gravity test";
};
}
