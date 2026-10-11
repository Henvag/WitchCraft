#include "DiagnosticWalk.h"
#include <cmath>

namespace witchcraft {
void DiagnosticWalk::Update(const GameFrame* game,const skycraft::proto::McState* mc,
    bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky,
    bool forward,bool stop,bool inputReady,bool nativeHidden) {
    using namespace skycraft::proto;
    if(state_==State::failed&&!haveWalk_) {
        if(followNative_&&game&&mc&&alive&&focused&&consumed&&(game->flags&kGamePlaying)&&
            (game->flags&kGameDiagnosticPatch)&&!(game->flags&(kGameMenu|kGameLoading))&&
            (mc->flags&kMcInWorld)&&!(mc->flags&(kMcDead|kMcFlying|kMcScreenOpen))) {
            settle_=DiagnosticSettle{};state_=State::settling;
        } else {
            settle_.Update(game,mc,alive,focused,consumed,now,sky);
            sky.flags=kSkyMenuOpen; return;
        }
    }
    auto fail=[&](const char* why) { state_=State::failed; reason_=why; };
    if(state_==State::settling) {
        if(followNative_&&settleSequence_) sky.teleportSeq=settleSequence_;
        settle_.Update(game,mc,alive,focused,consumed,now,sky);
        settleSequence_=sky.teleportSeq;
        reason_=settle_.Reason();
        if(settle_.Status()==DiagnosticSettle::State::failed) fail(settle_.Reason());
        if(settle_.Status()!=DiagnosticSettle::State::passed) return;
        // The same teleport remains acknowledged throughout the walk.
        anchor_=*game; teleport_=sky.teleportSeq; yaw_=sky.yaw;
        haveWalk_=true;
        start_[0]=last_[0]=mc->curX; start_[1]=last_[1]=mc->curY; start_[2]=last_[2]=mc->curZ;
        const double offset=std::hypot(start_[0]-anchor_.feet[0],start_[2]-anchor_.feet[2]);
        const double height=start_[1]-anchor_.feet[1];
        if(!std::isfinite(offset)||!std::isfinite(height)||offset>0.5||std::abs(height)>0.6) {
            fail("raw MC tick position did not confirm settled arrival");
            sky.flags=kSkyMenuOpen; return;
        }
        frame_=mc->frameCounter; freshAt_=startedAt_=now;
        state_=manual_?State::ready:State::walking;
        reason_=manual_?"ready: press W once; Esc or F8 returns Geralt":"forward key held for at most 250 ms";
    }
    if(Active()) {
        const bool valid=game&&mc&&alive&&focused&&(game->flags&kGamePlaying)&&
            (game->flags&kGameDiagnosticPatch)&&!(game->flags&(kGameMenu|kGameLoading))&&
            (mc->flags&kMcInWorld)&&!(mc->flags&(kMcDead|kMcFlying|kMcScreenOpen))&&
            game->worldId==anchor_.worldId&&game->relocation==anchor_.relocation&&
            game->collisionEpoch==anchor_.collisionEpoch&&mc->teleportAck==teleport_;
        if(manual_&&stop) {state_=state_==State::stopped?State::passed:State::cancelled;reason_="manual stop requested; returning Geralt";}
        else if(!valid) fail("focus, game state, link or acknowledgement changed");
        else if(manual_&&state_!=State::ready&&!inputReady) fail("manual input capture unavailable");
        else if(manual_&&state_!=State::ready&&!nativeHidden) fail("native visibility ownership lost; returning Geralt");
        else {
            double moved=0;
            for(int i=0;i<3;++i) moved+=std::pow(game->feet[i]-anchor_.feet[i],2);
            const double dx=mc->curX-start_[0],dy=mc->curY-start_[1],dz=mc->curZ-start_[2];
            distance_=std::hypot(dx,dz);
            double lag=0; const double raw[]{mc->curX,mc->curY,mc->curZ};
            for(int i=0;i<3;++i) lag+=std::pow(game->feet[i]-raw[i],2);
            if(!std::isfinite(moved)||(!followNative_&&moved>0.0625)||
                (followNative_&&(!std::isfinite(lag)||lag>0.5625||moved>6.25)))
                fail("Geralt movement or native follow lag exceeded bounds");
            else if(!std::isfinite(yaw_)||!std::isfinite(distance_)||!std::isfinite(dy)||distance_>2||std::abs(dy)>0.6)
                fail("MC left the bounded walking area or ground height");
            else {
                if(mc->frameCounter!=frame_) { frame_=mc->frameCounter; freshAt_=now; }
                if(now-freshAt_>1000) fail("MC position reports stopped updating");
                if(!(mc->flags&kMcOnGround)) {
                    if(!airborne_) { airborne_=true; airAt_=now; }
                    if(now-airAt_>250) fail("MC lost ground contact");
                } else airborne_=false;
                if(state_==State::ready) {
                    yaw_=sky.yaw;
                    if(now-startedAt_>30000) {state_=State::cancelled;reason_="no manual walk requested within 30 seconds; returning Geralt";}
                    else if(forward&&inputReady&&nativeHidden) {
                        state_=State::walking;startedAt_=now;
                        reason_="manual W press accepted; forward limited to 250 ms";
                    }
                } else if(state_==State::stopped&&now-brakeAt_>=16000) {
                    state_=State::passed;reason_="manual walk confirmed; automatic return after 15 seconds at rest";
                } else if(state_==State::walking&&(now-startedAt_>=250||(manual_&&!forward))) {
                    state_=State::braking; brakeAt_=now;
                    last_[0]=mc->curX; last_[1]=mc->curY; last_[2]=mc->curZ;
                    reason_="forward key released; checking stop";
                } else if(state_==State::braking&&now-brakeAt_>=1000) {
                    const double drift=std::hypot(mc->curX-last_[0],mc->curZ-last_[2]);
                    constexpr double radians=0.017453292519943295;
                    const double forward=-dx*std::sin(yaw_*radians)+dz*std::cos(yaw_*radians);
                    const double lateral=std::abs(dx*std::cos(yaw_*radians)+dz*std::sin(yaw_*radians));
                    if(airborne_) fail("MC had no ground contact at stop");
                    else if(distance_<0.25||forward<0.2) fail("no confirmed forward walk; terrain may obstruct movement");
                    else if(lateral>0.4||drift>0.35) fail("unexpected sideways movement or failure to stop");
                    else { state_=manual_?State::stopped:State::passed;
                        reason_=manual_?"manual walk stopped: Esc or F8 returns Geralt":"MC walked forward and stopped on exported ground"; }
                }
            }
        }
    }
    sky.teleportSeq=teleport_;
    sky.posX=anchor_.feet[0]; sky.posY=anchor_.feet[1]+0.5; sky.posZ=anchor_.feet[2];
    sky.yaw=yaw_;
    const bool active=Active();
    if(active) { sky.worldId=anchor_.worldId; sky.collisionEpoch=anchor_.collisionEpoch; }
    sky.flags=active?kSkyInGame:kSkyMenuOpen;
}
}
