#include "PlaySession.h"
#include <cmath>
namespace witchcraft {
void PlaySession::Update(const GameFrame* game,const skycraft::proto::McState* mc,
    bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky,
    std::uint8_t input,bool stop,bool inputReady,bool hidden,bool cameraOwned) {
    using namespace skycraft::proto;
    keys_=0;
    holdPhysics_=false;
    if(!active_&&!finished_) {
        if(sequence_) sky.teleportSeq=sequence_;
        auto arrival=mc?*mc:skycraft::proto::McState{};
        // A saved creative player may restart airborne. The arrival teleport
        // clears vanilla flight; allow it to begin rather than deadlocking the
        // gravity handoff on the old world's persisted ability state.
        if(cameraOwned&&(arrival.flags&kMcCanFly)) arrival.flags&=~kMcFlying;
        settle_.Update(game,mc?&arrival:nullptr,alive,focused,consumed,now,sky);
        sequence_=sky.teleportSeq;reason_=settle_.Reason();
        if(settle_.Status()==DiagnosticSettle::State::failed) {
            settle_=DiagnosticSettle{};reason_="waiting to settle after interruption";
        }
        if(settle_.Status()!=DiagnosticSettle::State::passed) return;
        anchor_=*game;active_=true;nativeGround_=game->feet[1];
        frame_=mc->frameCounter;freshAt_=now;
        const double dx=mc->curX-game->feet[0],dy=mc->curY-game->feet[1],dz=mc->curZ-game->feet[2];
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz)||std::hypot(dx,dz)>.5||std::abs(dy)>.6)
            Stop("raw Minecraft arrival did not confirm settled position");
    }
    if(active_) {
        const bool valid=game&&mc&&alive&&focused&&(game->flags&kGamePlaying)&&
            !(game->flags&(kGameMenu|kGameLoading))&&
            (mc->flags&kMcInWorld)&&!(mc->flags&kMcDead)&&(!(mc->flags&kMcFlying)||cameraOwned)&&
            game->worldId==anchor_.worldId&&game->collisionEpoch==anchor_.collisionEpoch&&
            game->relocation==anchor_.relocation&&mc->teleportAck==sequence_;
        if(!valid) Stop("world, collision coverage, focus or link interrupted; returning Geralt");
        else if(stop) Stop("F8 return requested; restoring Geralt");
        else if(started_&&(!inputReady||!hidden)) Stop("input capture or native hiding lost");
        else if(cameraOwned) {
            // Full play uses vanilla jump, fall, swim and flight physics. The
            // native actor is only a streaming/camera anchor; its vertical
            // animation cannot impose the old diagnostic landing deadlines.
            nativeGround_=game->feet[1];
            const bool finite=std::isfinite(mc->curX)&&std::isfinite(mc->curY)&&std::isfinite(mc->curZ);
            followLag_=finite?std::hypot(mc->curX-game->feet[0],mc->curZ-game->feet[2]):0;
            if(!finite) Stop("Minecraft reported a non-finite position");
            else if(followLag_>32||std::abs(mc->curY-nativeGround_)>256) Stop("position outside native streaming/camera range");
            if(mc->frameCounter!=frame_) {frame_=mc->frameCounter;freshAt_=now;}
            if(now-freshAt_>1000) Stop("Minecraft position reports stalled");
            if(!catchingUp_&&followLag_>1.25) catchingUp_=true;
            else if(catchingUp_&&followLag_<.5) catchingUp_=false;
            // Flight or gliding well above the native ground anchor does not
            // need fresh support underneath; low flight and landings still wait.
            const bool airborne=(mc->flags&(kMcFlying|kMcFallFlying))&&mc->curY-nativeGround_>6;
            const bool coverage=airborne||(game->flags&(kGamePlayPatch|kGameDiagnosticPatch))!=0;
            // Pause physics, including inertia/gravity, rather than merely
            // releasing W while an airborne player continues past the patch.
            holdPhysics_=active_&&(catchingUp_||!coverage);
            const bool gui=(mc->flags&kMcScreenOpen)!=0;
            if(active_) {
                started_|=inputReady&&hidden;
                keys_=inputReady&&hidden&&!gui&&!holdPhysics_?static_cast<std::uint8_t>(input&31):0;
                reason_=!coverage?"holding Minecraft for terrain delivery; F8 returns Geralt":catchingUp_?
                    "holding Minecraft while native anchor catches up; F8 returns Geralt":gui?
                    "Minecraft inventory/menu active":"continuous Minecraft physics; F8 returns Geralt";
            }
        } else {
            const bool coverage=(game->flags&(kGamePlayPatch|kGameDiagnosticPatch))!=0;
            if(!coverage&&!waitingCoverage_) {waitingCoverage_=true;coverageAt_=now;}
            if(coverage) waitingCoverage_=false;
            if(waitingCoverage_&&now-coverageAt_>5000) Stop("terrain streaming did not restore coverage within five seconds");
            const bool gui=(mc->flags&kMcScreenOpen)!=0,ground=(mc->flags&kMcOnGround)!=0;
            const bool flight=cameraOwned&&(mc->flags&kMcCanFly);
            const bool wet=(mc->flags&(kMcInWater|kMcSwimming))!=0;
            nativeGround_=game->feet[1];
            if(!std::isfinite(mc->curX)||!std::isfinite(mc->curY)||!std::isfinite(mc->curZ)) Stop("Minecraft reported a non-finite position");
            // Minecraft also owns support on player-placed blocks. The hidden
            // Witcher anchor stays on its terrain underneath small platforms.
            const double elevation=mc->curY-nativeGround_;
            if(flight) {
                jumping_=tookOff_=air_=false;
                if(std::abs(elevation)>256) Stop("flight outside camera altitude range");
            }
            if(wet&&!flight) {
                jumping_=tookOff_=air_=false;
                if(elevation<-.6||elevation>3.7) Stop("water outside supported camera height range");
            }
            if(!flight&&ground&&(elevation<-.6||elevation>2.1)) Stop("Minecraft support outside grounded camera range (-0.6 to +2.1 m)");
            const bool space=(input&16)&&inputReady&&hidden&&!gui;
            if(space&&!spaceDown_&&ground&&!wet&&!flight&&!jumping_&&!catchingUp_&&!waitingCoverage_) {
                jumping_=true;tookOff_=false;jumpAt_=now;jumpGround_=mc->curY;peak_=0;
                nativeGround_=game->feet[1];
            }
            spaceDown_=space;
            if(active_&&jumping_) {
                // Native ground snapping is authoritative for the hidden camera
                // anchor; Minecraft's airborne height is rendered independently.
                nativeGround_=game->feet[1];
                const double height=mc->curY-jumpGround_;peak_=std::fmax(peak_,height);
                if(!std::isfinite(height)||height>1.6||height<-2.1||elevation<-.6||elevation>3.7) Stop("jump left supported height range");
                else if(!ground) tookOff_=true;
                else if(tookOff_) jumping_=false;
                else if(now-jumpAt_>750) Stop("jump takeoff timed out");
                if(jumping_&&now-jumpAt_>3000) Stop("jump landing timed out");
            }
            if(active_&&!ground&&!jumping_&&!wet&&!flight) {
                if(!air_) {air_=true;airAt_=now;}
                // Allow stepping off a small Minecraft platform to the sampled
                // terrain, without enabling unbounded falls or flight.
                if(elevation<-.6||elevation>3.7||now-airAt_>1500) Stop("fall outside grounded camera range; returning Geralt");
            } else air_=false;
            double target[3]{};PilotFeet(*mc,target);double lag=0;
            for(int i=0;i<3;++i) lag+=std::pow(game->feet[i]-target[i],2);
            followLag_=std::sqrt(lag);
            if(!std::isfinite(lag)||lag>4) Stop("native follow lag exceeded 2 m");
            else if(!catchingUp_&&lag>1.5625) {catchingUp_=true;catchUpAt_=now;}
            else if(catchingUp_&&lag<=.25) catchingUp_=false;
            if(catchingUp_&&now-catchUpAt_>1000) Stop("native follow did not catch up after input release");
            if(mc->frameCounter!=frame_) {frame_=mc->frameCounter;freshAt_=now;}
            if(now-freshAt_>1000) Stop("Minecraft position reports stalled");
            if(active_) {
                started_|=inputReady&&hidden;
                keys_=inputReady&&hidden&&!gui&&!catchingUp_&&!waitingCoverage_?static_cast<std::uint8_t>(input&15):0;
                if((wet||flight)&&inputReady&&hidden&&!gui&&!catchingUp_&&!waitingCoverage_) keys_|=input&16;
                if(!gui&&jumping_&&!tookOff_&&now-jumpAt_<100) keys_|=16;
                reason_=waitingCoverage_?"waiting for terrain stream; Minecraft remains active":catchingUp_?"briefly releasing movement while native anchor catches up":gui?"Minecraft inventory/menu active":flight?"Minecraft creative flight; Space rises, Shift descends":wet?"Minecraft water movement; grounded native camera anchor":jumping_?"walking/jumping; grounded native camera anchor":"Minecraft play active; F8 returns Geralt";
            }
        }
    }
    sky.teleportSeq=sequence_;sky.posX=anchor_.feet[0];sky.posY=anchor_.feet[1]+.5;sky.posZ=anchor_.feet[2];
    sky.worldId=anchor_.worldId;sky.collisionEpoch=anchor_.collisionEpoch;
    sky.flags=active_?(kSkyInGame|(cameraOwned?kSkyManagedTerrain:0)|(holdPhysics_?kSkyHoldPhysics:0)):kSkyMenuOpen;
}
}
