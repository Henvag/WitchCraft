#pragma once
#include "DiagnosticSettle.h"
#include "DiagnosticJump.h"
namespace witchcraft {
class DiagnosticMovement {
public:
    explicit DiagnosticMovement(bool jumpTrial=false):jumpTrial_(jumpTrial) {}
    enum class State {settling,ready,controlling,braking,stopped,passed,failed,cancelled};
    void Update(const GameFrame*,const skycraft::proto::McState*,bool alive,bool focused,
        bool consumed,std::uint64_t now,skycraft::proto::SkyState&,std::uint8_t keys,
        bool stop,bool inputReady,bool hidden);
    bool Active() const {return state_==State::ready||state_==State::controlling||state_==State::braking||state_==State::stopped;}
    State Status() const {return state_;}
    const char* Reason() const {return reason_;}
    std::uint8_t Keys() const {return keys_;}
    double Distance() const {return distance_;}
    double Path() const {return path_;}
    const DiagnosticJump& Jump() const {return jump_;}
    void PilotFeet(const skycraft::proto::McState& mc,double* out) const {
        out[0]=mc.curX;out[1]=jumpTrial_?anchor_.feet[1]:mc.curY;out[2]=mc.curZ;
    }
    void Stop(const char* reason) {state_=State::failed;reason_=reason;keys_=0;entered_=true;}
private:
    bool jumpTrial_=false;
    DiagnosticJump jump_;
    DiagnosticSettle settle_;
    State state_=State::settling;
    GameFrame anchor_{};
    std::uint32_t sequence_=0;
    std::uint64_t readyAt_=0,startedAt_=0,freshAt_=0,frame_=0,restAt_=0,airAt_=0,brakeAt_=0;
    bool entered_=false,started_=false,rest_=false,air_=false;
    double origin_[3]{},last_[3]{},distance_=0,path_=0;
    std::uint8_t keys_=0;
    const char* reason_="waiting for stationary player and collision delivery";
};
}
