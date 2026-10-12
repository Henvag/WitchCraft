#pragma once
#include "RenderTiming.h"
#include <array>

namespace witchcraft {
// SkyCraft's tick-history approach: interpolate known physics segments on a
// filtered tick clock, with a small buffer for late arrivals. Never extrapolate.
class MotionTimeline {
    struct Tick { skycraft::proto::McState state{}; std::int64_t at=0; };
    std::array<Tick,8> ticks_{};
    unsigned count_=0;
    std::int64_t frequency_=0,lastFrame_=0;
    double delayMs_=20;
    std::array<double,40> due_{};
    unsigned dueCount_=0,dueNext_=0;
    bool heldMoving_=false;
public:
    void Reset() { count_=0;frequency_=lastFrame_=0;delayMs_=20;dueCount_=dueNext_=0; }
    double DelayMs() const { return delayMs_; }
    bool HeldMoving() const { return heldMoving_; }
    skycraft::proto::McState Render(const skycraft::proto::McState& input,
        std::int64_t now,std::int64_t frequency,double leadMs=0) {
        using namespace skycraft::proto;
        heldMoving_=false;
        auto mc=input;
        // Render and game callbacks can consume snapshots out of order. An
        // older packet must not rewind or reset their shared tick history.
        if(count_&&mc.frameCounter<ticks_[count_-1].state.frameCounter&&
            mc.tickQpc<=ticks_[count_-1].state.tickQpc) mc=ticks_[count_-1].state;
        if(frequency<1000||mc.tickQpc<=0||!std::isfinite(mc.tickMs)||
            mc.tickMs<1||mc.tickMs>1000||!(mc.flags&kMcInWorld)) {Reset();return mc;}
        if(mc.flags&kMcScreenOpen) {Reset();return mc;}
        double distance=0;
        for(unsigned i=0;i<3;++i) {
            const double before[]{mc.prevX,mc.prevY,mc.prevZ},after[]{mc.curX,mc.curY,mc.curZ};
            if(!std::isfinite(before[i])||!std::isfinite(after[i])) {Reset();return mc;}
            distance+=std::pow(after[i]-before[i],2);
        }
        if(distance>16) {Reset();return mc;}
        if(frequency_&&frequency_!=frequency) Reset();
        frequency_=frequency;
        const auto period=static_cast<std::int64_t>(std::llround(mc.tickMs*frequency/1000));
        if(count_&&(mc.teleportAck!=ticks_[count_-1].state.teleportAck||
            mc.tickQpc<ticks_[count_-1].state.tickQpc)) Reset();
        frequency_=frequency;
        if(!count_||mc.tickQpc!=ticks_[count_-1].state.tickQpc) {
            Tick tick{mc,mc.tickQpc};
            if(count_) {
                const auto& last=ticks_[count_-1];
                const auto slots=std::llround(static_cast<double>(mc.tickQpc-last.state.tickQpc)/period);
                const auto error=mc.tickQpc-(last.at+slots*period);
                if(slots>=1&&slots<=10&&std::abs(error)<period*3/10)
                    tick.at=last.at+slots*period+error/16;
                else if(slots>10) {Reset();frequency_=frequency;}
                if(lastFrame_) {
                    const double due=(lastFrame_-tick.at)*1000.0/frequency;
                    if(due>=-30&&due<30) {
                        due_[dueNext_++%due_.size()]=due;
                        dueCount_=std::min<unsigned>(dueCount_+1,due_.size());
                    }
                }
            }
            if(count_==ticks_.size()) {
                for(unsigned i=1;i<count_;++i) ticks_[i-1]=ticks_[i];
                --count_;
            }
            ticks_[count_++]=tick;
        } else if(mc.frameCounter>=ticks_[count_-1].state.frameCounter)
            ticks_[count_-1].state=mc;
        const double dt=lastFrame_?std::clamp((now-lastFrame_)*1.0/frequency,0.0,.1):0;
        lastFrame_=std::max(lastFrame_,now);
        if(dueCount_) {
            const double target=std::clamp(*std::max_element(due_.begin(),due_.begin()+dueCount_)+1,4.0,30.0);
            delayMs_=target>delayMs_?std::min(target,delayMs_+20*dt):std::max(target,delayMs_-2*dt);
        }
        if(!std::isfinite(leadMs)||leadMs<0||leadMs>20) leadMs=0;
        const double at=now+(leadMs-delayMs_)*frequency/1000;
        unsigned index=0;
        for(unsigned i=0;i<count_;++i) if(ticks_[i].at<=at) index=i;
        const auto& tick=ticks_[index];
        auto result=mc;
        const double alpha=std::clamp((at-tick.at)/period,0.0,1.0);
        const auto& s=tick.state;
        heldMoving_=index+1==count_&&at>tick.at+period&&
            std::hypot(s.curX-s.prevX,s.curZ-s.prevZ)>.001;
        result.x=s.prevX+(s.curX-s.prevX)*alpha;
        result.y=s.prevY+(s.curY-s.prevY)*alpha;
        result.z=s.prevZ+(s.curZ-s.prevZ)*alpha;
        if(at>tick.at+period&&index+1<count_) {
            const auto& next=ticks_[index+1];
            const double gap=next.at-(tick.at+period);
            const double blend=gap>0?std::clamp((at-tick.at-period)/gap,0.0,1.0):1;
            result.x=s.curX+(next.state.prevX-s.curX)*blend;
            result.y=s.curY+(next.state.prevY-s.curY)*blend;
            result.z=s.curZ+(next.state.prevZ-s.curZ)*blend;
        }
        return result;
    }
};
skycraft::proto::McState RenderMotionState(const skycraft::proto::McState& mc,
    std::int64_t now,std::int64_t frequency,double leadMs=0);
struct MotionStats { unsigned renderFrames=0,heldMovingFrames=0;double bufferMs=0; };
MotionStats ReadMotionStats();
}
