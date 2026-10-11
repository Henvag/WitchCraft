#pragma once
#include <cmath>
#include <cstdint>
namespace witchcraft {
// Standing jumps require a new Space press after a confirmed landing.
class DiagnosticJump {
public:
    enum class State {waiting,launching,airborne,landing,passed,failed};
    void Update(std::uint64_t now,bool request,bool ground,double height,double distance) {
        if(state_==State::failed) return;
        const bool press=request&&!requestDown_;requestDown_=request;
        if(!std::isfinite(height)||!std::isfinite(distance)||distance>0.4||height>1.6||height<-.25) {Fail("jump exceeded standing area or height bound");return;}
        peak_=std::fmax(peak_,height);
        if(state_==State::passed&&press&&ground&&std::abs(height)<=.15) {
            state_=State::launching;started_=now;peak_=height;
        }
        if(state_==State::waiting) {
            if(!ground) {Fail("ground contact lost before jump request");return;}
            if(request) {state_=State::launching;started_=now;}
        } else if(state_==State::launching) {
            if(!ground) state_=State::airborne;
            else if(now-started_>750) Fail("Space did not produce takeoff");
        } else if(state_==State::airborne) {
            if(ground) {
                if(peak_<.25||std::abs(height)>.15) Fail("jump landing height or takeoff not confirmed");
                else {state_=State::landing;landed_=now;lastHeight_=height;}
            }
        } else if(state_==State::landing) {
            if(!ground||std::abs(height)>.15) {Fail("ground contact lost while confirming landing");return;}
            if(std::abs(height-lastHeight_)>.02) landed_=now;
            lastHeight_=height;
            if(now-landed_>=500) state_=State::passed;
        } else if(state_==State::passed&&(!ground||std::abs(height)>.15)) Fail("ground contact lost after landing");
        if(Running()&&now-started_>3000) Fail("jump did not land within three seconds");
    }
    bool Running() const {return state_==State::launching||state_==State::airborne||state_==State::landing;}
    bool Pulse(std::uint64_t now) const {return state_==State::launching&&now-started_<100;}
    State Status() const {return state_;}
    double Peak() const {return peak_;}
    const char* Reason() const {return reason_;}
private:
    void Fail(const char* reason) {state_=State::failed;reason_=reason;}
    State state_=State::waiting;
    std::uint64_t started_=0,landed_=0;
    double peak_=0,lastHeight_=0;
    bool requestDown_=false;
    const char* reason_="single jump and landing trial";
};
}
