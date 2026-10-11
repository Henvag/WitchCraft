#include "DiagnosticMovement.h"
#include <cmath>
namespace witchcraft {
void DiagnosticMovement::Update(const GameFrame* game,const skycraft::proto::McState* mc,
    bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky,
    std::uint8_t input,bool stop,bool inputReady,bool hidden) {
    using namespace skycraft::proto;
    keys_=0;
    if(state_==State::failed&&!entered_&&game&&mc&&alive&&focused&&consumed&&
        (game->flags&kGamePlaying)&&(game->flags&kGameDiagnosticPatch)&&!(game->flags&(kGameMenu|kGameLoading))&&
        (mc->flags&kMcInWorld)&&!(mc->flags&(kMcScreenOpen|kMcDead|kMcFlying))) {
        settle_=DiagnosticSettle{};state_=State::settling;
    }
    if(state_==State::settling) {
        if(sequence_) sky.teleportSeq=sequence_;
        settle_.Update(game,mc,alive,focused,consumed,now,sky);sequence_=sky.teleportSeq;reason_=settle_.Reason();
        if(settle_.Status()==DiagnosticSettle::State::failed) state_=State::failed;
        if(settle_.Status()!=DiagnosticSettle::State::passed) return;
        anchor_=*game;entered_=true;
        const double raw[]{mc->curX,mc->curY,mc->curZ};
        for(int i=0;i<3;++i) origin_[i]=last_[i]=raw[i];
        if(!std::isfinite(raw[0])||!std::isfinite(raw[1])||!std::isfinite(raw[2])||
            std::hypot(raw[0]-game->feet[0],raw[2]-game->feet[2])>0.5||std::abs(raw[1]-game->feet[1])>0.6) {
            Stop("raw MC arrival did not confirm settled position");sky.flags=kSkyMenuOpen;return;
        }
        readyAt_=freshAt_=now;frame_=mc->frameCounter;
        state_=State::ready;reason_=jumpTrial_?"Space ready for one standing jump; F8 returns Geralt":"WASD ready; F8 or Esc returns Geralt";
    }
    if(Active()) {
        const bool valid=game&&mc&&alive&&focused&&(game->flags&kGamePlaying)&&(game->flags&kGameDiagnosticPatch)&&
            !(game->flags&(kGameMenu|kGameLoading))&&(mc->flags&kMcInWorld)&&
            !(mc->flags&(kMcScreenOpen|kMcDead|kMcFlying))&&game->worldId==anchor_.worldId&&
            game->collisionEpoch==anchor_.collisionEpoch&&game->relocation==anchor_.relocation&&mc->teleportAck==sequence_;
        if(!valid) Stop("focus, world, collision coverage, link or acknowledgement changed");
        else if(started_&&(!inputReady||!hidden)) Stop("input capture or native hiding lost");
        else {
            const double raw[]{mc->curX,mc->curY,mc->curZ};
            double lag=0,nativeTravel=0;
            for(int i=0;i<3;++i) {
                const double target=jumpTrial_&&i==1?anchor_.feet[1]:raw[i];
                lag+=std::pow(game->feet[i]-target,2);nativeTravel+=std::pow(game->feet[i]-anchor_.feet[i],2);
            }
            distance_=std::hypot(raw[0]-origin_[0],raw[2]-origin_[2]);
            const double height=raw[1]-origin_[1];
            if(jumpTrial_) {
                jump_.Update(now,inputReady&&hidden&&(input&16),mc->flags&kMcOnGround,height,distance_);
                if(jump_.Status()==DiagnosticJump::State::failed) Stop(jump_.Reason());
                if(jump_.Running()&&(!started_||state_==State::stopped)) {started_=true;startedAt_=now;state_=State::controlling;}
            }
            if(!Active()) {}
            else if(!std::isfinite(lag)||!std::isfinite(nativeTravel)||lag>0.5625||nativeTravel>16||
                (jumpTrial_&&std::abs(game->feet[1]-anchor_.feet[1])>.15))
                Stop("native follow lag or native travel exceeded bound");
            else if(!std::isfinite(distance_)||!std::isfinite(height)||!std::isfinite(sky.yaw)||!std::isfinite(sky.pitch)||distance_>3||(!jumpTrial_&&std::abs(height)>0.6))
                Stop("MC exceeded 3 m area or ground-height bound");
            else {
                const double step=std::hypot(raw[0]-last_[0],raw[2]-last_[2]);
                if(mc->frameCounter!=frame_) {frame_=mc->frameCounter;freshAt_=now;path_+=step;for(int i=0;i<3;++i) last_[i]=raw[i];}
                if(now-freshAt_>1000) Stop("MC reports stopped updating");
                if(!(mc->flags&kMcOnGround)) {
                    if(!air_) {air_=true;airAt_=now;}
                    if(now-airAt_>250&&!(jumpTrial_&&jump_.Running())) Stop("MC lost ground contact");
                } else air_=false;
                if(Active()) {
                    if((!input||state_==State::braking||state_==State::stopped)&&step<0.01&&!air_) {if(!rest_) {rest_=true;restAt_=now;}}
                    else rest_=false;
                    if(stop) {
                        state_=(jumpTrial_?jump_.Status()==DiagnosticJump::State::passed:path_>=0.25)&&rest_&&now-restAt_>=500?State::passed:State::cancelled;
                        reason_="manual return requested; restoring Geralt";
                    } else if(state_==State::ready||state_==State::controlling) {
                        if((started_&&!jumpTrial_&&(now-startedAt_>=20000||path_>=7||distance_>=2.3))||(!started_&&now-readyAt_>=30000)) {
                            state_=State::braking;brakeAt_=now;rest_=false;reason_="trial time, path or radius limit; releasing WASD";
                        } else if(jumpTrial_) {
                            keys_=inputReady&&hidden&&jump_.Pulse(now)?16:0;
                            reason_=started_?"Minecraft jump active; ground camera anchor retained":"Space ready for standing jump";
                            if(jump_.Status()==DiagnosticJump::State::passed) {state_=State::stopped;brakeAt_=now;reason_="landed; Space jumps again, F8 returns Geralt";}
                        } else if(inputReady&&hidden) {
                            keys_=input&15;
                            if(keys_&&!started_) {started_=true;startedAt_=now;state_=State::controlling;}
                            reason_=keys_?"manual WASD movement active":"WASD released; ready for another direction or F8";
                        }
                    } else if(state_==State::braking) {
                        // Physical keys may still be held, but desired MC keys
                        // are zero. Check position stability independently.
                        if(step<0.01&&!air_) {if(!rest_) {rest_=true;restAt_=now;}}
                        else rest_=false;
                        if(rest_&&now-restAt_>=500) {state_=State::stopped;reason_="bounded movement stopped; F8 returns Geralt";brakeAt_=now;}
                        else if(now-brakeAt_>3000) Stop("MC did not stop after WASD release");
                    } else if(state_==State::stopped&&!jumpTrial_&&now-brakeAt_>=10000) {
                        state_=(jumpTrial_?jump_.Status()==DiagnosticJump::State::passed:path_>=0.25)?State::passed:State::cancelled;reason_="automatic return after bounded movement stopped";
                    }
                }
            }
        }
    }
    if(!Active()) keys_=0;
    sky.teleportSeq=sequence_;sky.posX=anchor_.feet[0];sky.posY=anchor_.feet[1]+0.5;sky.posZ=anchor_.feet[2];
    if(Active()) {sky.worldId=anchor_.worldId;sky.collisionEpoch=anchor_.collisionEpoch;}
    sky.flags=Active()?kSkyInGame:kSkyMenuOpen;
    // Session's native camera yaw/pitch are retained, including between inputs.
}
}
