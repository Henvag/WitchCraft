#include "NativePilot.h"
#include <cstdlib>
#include <limits>
using namespace witchcraft::native;
int main() {
    if(PilotVisibilityFor(false,0)!=PilotVisibility::Acquire||
        PilotVisibilityFor(true,kPilotHideReason)!=PilotVisibility::Retain||
        PilotVisibilityFor(true,0)!=PilotVisibility::Reapply) return 65;
    // A native animation may clear our reason while retaining its own. The
    // active session re-applies only ours; a fresh session must not take over
    // a player already hidden by a cutscene or another game system.
    if(PilotVisibilityFor(true,1)!=PilotVisibility::Reapply||
        PilotVisibilityFor(true,kPilotHideReason|1)!=PilotVisibility::Retain||
        PilotVisibilityFor(false,1)!=PilotVisibility::GameOwned) return 66;
    witchcraft::PilotCommand lifetime;
    if(PilotCommandFresh(lifetime,1000)) return 54;
    lifetime.publishedAt=1000;
    if(!PilotCommandFresh(lifetime,1151)||!PilotCommandFresh(lifetime,2000)||
        PilotCommandFresh(lifetime,2001)||PilotCommandFresh(lifetime,999)) return 55;
    // Animation/controller rejection retries are bounded without delaying a
    // newly accepted follow or a fresh handoff.
    PilotRetryCadence retry;
    if(!retry.Due(100)) return 56;
    retry.Completed(false,100);
    if(retry.Due(115)||!retry.Due(116)) return 57;
    retry.Completed(false,116);
    if(retry.Due(147)||!retry.Due(148)) return 58;
    retry.Completed(false,148);retry.Completed(false,212);
    if(retry.Due(311)||!retry.Due(312)) return 59;
    retry.Completed(false,312);
    if(retry.Due(411)||!retry.Due(412)) return 60;
    retry.Completed(true,412);
    if(!retry.Due(413)) return 61;
    retry.Completed(false,413);retry.Reset();
    if(!retry.Due(414)) return 62;
    witchcraft::PilotCommand follow;
    follow.groundFollow=follow.interpolateFollow=true;
    follow.feet[1]=10;
    follow.followState.tickQpc=1000;follow.followState.tickMs=50;
    follow.followState.prevX=100;follow.followState.curX=100.2;
    follow.followState.prevZ=-200;follow.followState.curZ=-200.2;
    double early[3]{},late[3]{};
    ResolvePilotFeet(follow,1010,1000,early);
    ResolvePilotFeet(follow,1030,1000,late);
    if(std::abs(early[0]-100.04)>1e-8||std::abs(late[0]-100.12)>1e-8||late[1]!=10) return 30;
    ResolvePilotFeet(follow,1075,1000,late);
    if(late[0]!=100.2||late[2]!=-200.2) return 31; // Never extrapolate past the known tick.
    ResolvePilotFeet(follow,1030,1000,late,1000.0/60);
    if(late[0]<=100.12||late[0]>100.2||late[1]!=10) return 33;
    ResolvePilotFeet(follow,1049,1000,late,1000.0/60);
    if(late[0]!=100.2||late[2]!=-200.2) return 34; // Lead stops at known position.
    auto fresh=follow.followState;fresh.flags=skycraft::proto::kMcInWorld;
    fresh.frameCounter=10;fresh.tickQpc=1050;fresh.prevX=100.2;fresh.curX=100.4;
    if(!UseLatestFollowState(follow,fresh)) return 35;
    ResolvePilotFeet(follow,1060,1000,late);
    if(std::abs(late[0]-100.24)>1e-8) return 36; // New tick supersedes overlay snapshot.
    auto invalid=fresh;++invalid.teleportAck;
    if(UseLatestFollowState(follow,invalid)) return 37;
    invalid=fresh;invalid.flags|=skycraft::proto::kMcFlying;
    if(UseLatestFollowState(follow,invalid)) return 38;
    invalid=fresh;invalid.frameCounter=9;
    if(UseLatestFollowState(follow,invalid)) return 39;
    invalid=fresh;invalid.tickQpc=1049;
    if(UseLatestFollowState(follow,invalid)) return 40;
    invalid=fresh;invalid.curY=std::numeric_limits<double>::quiet_NaN();
    if(UseLatestFollowState(follow,invalid)) return 63;
    invalid=fresh;invalid.prevX=1000001;
    if(UseLatestFollowState(follow,invalid)) return 64;
    follow.interpolateFollow=false;follow.feet[0]=42;
    ResolvePilotFeet(follow,1030,1000,late);
    if(late[0]!=42||late[1]!=10) return 32; // Diagnostics keep their exact target.
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
    witchcraft::PilotCommand flight;
    flight.followState.flags=skycraft::proto::kMcFlying;
    if(GroundSnapLimit(flight)!=.5) return 41;
    flight.cameraOwned=true;if(GroundSnapLimit(flight)!=4) return 42;
    float waterTarget[]{-528.176f,-1187.978f,65.292f},waterActual[]{-528.176f,-1187.978f,66.013f};
    if(!motion.Begin(waterTarget,2400,true,GroundSnapLimit(flight))||
        motion.Observe(waterActual,2416)!=PilotAck::Confirmed) return 43;
    waterActual[0]-=.2f;
    if(!motion.Begin(waterTarget,2500,true,4)||motion.Observe(waterActual,2516)!=PilotAck::Waiting||
        motion.Observe(waterActual,2751)!=PilotAck::TimedOut) return 44; // Horizontal confirmation remains strict.
    waterActual[0]=waterTarget[0];waterActual[2]=waterTarget[2]+4.01f;
    if(!motion.Begin(waterTarget,2800,true,4)||motion.Observe(waterActual,2816)!=PilotAck::TimedOut) return 45;
    if(motion.Begin(waterTarget,2900,true,5)) return 46;
    float controllerTarget[]{-590.58f,-1156.734f,73.779f};
    float controllerActual[]{-590.522f,-1156.734f,73.779f};
    if(!motion.Begin(controllerTarget,3000,true,4,.1)||
        motion.Observe(controllerActual,3016)!=PilotAck::Confirmed) return 47;
    controllerActual[0]=controllerTarget[0]+.088f;
    if(!motion.Begin(controllerTarget,3100,true,.5,.1)||
        motion.Observe(controllerActual,3116)!=PilotAck::Confirmed) return 48;
    if(!motion.Begin(controllerTarget,3200,true)||
        motion.Observe(controllerActual,3216)!=PilotAck::Waiting) return 49;
    motion.Reset();controllerActual[0]=controllerTarget[0]+.11f;
    if(!motion.Begin(controllerTarget,3300,true,4,.1)||
        motion.Observe(controllerActual,3316)!=PilotAck::Waiting||
        motion.Observe(controllerActual,3551)!=PilotAck::TimedOut) return 50;
    if(motion.Begin(controllerTarget,3600,true,4,.101)||
        motion.Begin(controllerTarget,3600,true,4,std::numeric_limits<double>::quiet_NaN())) return 51;
    double catchup[]{103,10,-200};
    if(PilotFollowPosition(current,catchup,out)||!PilotFollowPosition(current,catchup,out,32)||
        std::abs(out[0]-100.5f)>.001f) return 52; // Animation displacement catches up through small steps.
    catchup[0]=133;if(PilotFollowPosition(current,catchup,out,32)) return 53;
}
