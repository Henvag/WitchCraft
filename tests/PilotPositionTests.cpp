#include "NativePilot.h"
#include <cstdlib>
#include <limits>
using namespace witchcraft::native;
int main() {
    float current[]{100,200,10},out[4]{};double mc[]{100.2,10,-200.1};
    if(!PilotPosition(current,mc,out)||std::abs(out[0]-100.2f)>0.001f||std::abs(out[1]-200.1f)>0.001f||out[2]!=10||out[3]!=1) return 1;
    mc[0]=101;if(PilotPosition(current,mc,out)) return 2;
    mc[0]=std::numeric_limits<double>::quiet_NaN();if(PilotPosition(current,mc,out)) return 3;
    mc[0]=100;mc[1]=10.8;if(PilotPosition(current,mc,out)) return 4;
    PilotMotion motion;
    float target[]{100.3f,200,10},settled[]{100.3f,200,10.01f};
    if(motion.Observe(current,0)!=PilotAck::Idle||!motion.Begin(target,100)) return 5;
    if(motion.Observe(current,116)!=PilotAck::Waiting||motion.Begin(target,120)) return 6;
    if(motion.Observe(settled,132)!=PilotAck::Confirmed||motion.Observe(settled,148)!=PilotAck::Idle) return 7;
    if(!motion.Begin(target,200)||motion.Observe(current,450)!=PilotAck::Waiting||motion.Observe(current,451)!=PilotAck::TimedOut) return 8;
    if(!motion.Begin(target,500)) return 9;
    motion.Reset();if(motion.Observe(settled,516)!=PilotAck::Idle) return 10;
    target[0]=std::numeric_limits<float>::quiet_NaN();if(motion.Begin(target,600)) return 11;
    target[0]=100.3f;if(!motion.Begin(target,700)||motion.Observe(settled,951)!=PilotAck::TimedOut) return 12;
    target[2]=11;float airborne[]{100.3f,200,10.9f};
    if(!motion.Begin(target,1000)||motion.Observe(airborne,1016)!=PilotAck::Waiting) return 13;
    airborne[0]=100.4f;if(motion.Observe(airborne,1132)!=PilotAck::Waiting) return 16;
    airborne[0]=100.3f;airborne[2]=11.1f;if(motion.Observe(airborne,1148)!=PilotAck::Waiting) return 17;
    airborne[2]=10.9f;if(motion.Observe(airborne,1251)!=PilotAck::TimedOut) return 18;
    if(!std::isfinite(motion.MinimumError())||motion.MinimumError()>.151) return 19;
    airborne[2]=10.93f;
    if(!motion.Begin(target,1400)||motion.Observe(airborne,1416)!=PilotAck::Waiting) return 20;
    if(motion.Observe(airborne,1651)!=PilotAck::TimedOut) return 21;
    double far[]{101.2,10,-200};
    if(!PilotFollowPosition(current,far,out)||std::abs(out[0]-100.5f)>.001f||out[1]!=200||out[2]!=10||out[3]!=1) return 22;
    // The converted diagonal displacement has a 0.5 m length, not 0.5 per axis.
    far[2]=-201.2;
    if(!PilotFollowPosition(current,far,out)||std::abs(std::hypot(out[0]-100,out[1]-200)-.5)>.001) return 23;
    far[0]=102.01;far[2]=-200;if(PilotFollowPosition(current,far,out)) return 24;
    far[0]=std::numeric_limits<double>::quiet_NaN();if(PilotFollowPosition(current,far,out)) return 25;
    far[0]=100.2;if(!PilotFollowPosition(current,far,out)||std::abs(out[0]-100.2f)>.001f) return 26;
    float slopeTarget[]{100.3f,200,10},slopeActual[]{100.3f,200,10.07f};
    if(!motion.Begin(slopeTarget,2000,true)||motion.Observe(slopeActual,2016)!=PilotAck::Confirmed) return 27;
    if(!motion.Begin(slopeTarget,2100)||motion.Observe(slopeActual,2116)!=PilotAck::Waiting) return 28;
    motion.Reset();slopeActual[0]=100.4f;
    if(!motion.Begin(slopeTarget,2200,true)||motion.Observe(slopeActual,2216)!=PilotAck::Waiting) return 29;
    motion.Reset();slopeActual[0]=100.3f;slopeActual[2]=10.51f;
    if(!motion.Begin(slopeTarget,2300,true)||motion.Observe(slopeActual,2316)!=PilotAck::TimedOut) return 30;
}
