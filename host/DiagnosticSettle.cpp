#include "DiagnosticSettle.h"
#include <cmath>

namespace witchcraft {
void DiagnosticSettle::Update(const GameFrame* game,const skycraft::proto::McState* mc,
    bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky) {
    using namespace skycraft::proto;
    auto same=[&] {
        if (!game||!haveAnchor_||game->worldId!=anchor_.worldId||game->relocation!=anchor_.relocation||
            game->collisionEpoch!=anchor_.collisionEpoch) return false;
        double distance=0;
        for(int i=0;i<3;++i) distance+=std::pow(game->feet[i]-anchor_.feet[i],2);
        return std::isfinite(distance)&&distance<=0.0625;
    };
    const bool eligible=game&&mc&&alive&&focused&&(game->flags&kGamePlaying)&&
        (game->flags&kGameDiagnosticPatch)&&!(game->flags&(kGameMenu|kGameLoading))&&
        (mc->flags&kMcInWorld)&&!(mc->flags&(kMcDead|kMcFlying|kMcScreenOpen));
    if(state_==State::waiting) {
        if(!eligible) { haveAnchor_=false; return; }
        if(!same()) { anchor_=*game; haveAnchor_=true; stableAt_=now; return; }
        if(now-stableAt_<2000||!consumed) return;
        teleport_=sky.teleportSeq+1; if(!teleport_) ++teleport_;
        if(teleport_==mc->teleportAck&&++teleport_==0) ++teleport_;
        startedAt_=now; state_=State::running;
        reason_="MC gravity test started; no input or Geralt control";
    } else if(state_==State::running) {
        if(!eligible||!same()) {
            state_=State::failed; reason_="handoff, movement, stale link or world change";
        } else if(now-startedAt_>15000) {
            state_=State::failed; reason_="timed out waiting for released MC player to settle";
        }
    }
    if(state_==State::running&&mc->teleportAck==teleport_) {
        const double dx=mc->x-anchor_.feet[0],dz=mc->z-anchor_.feet[2],dy=mc->y-anchor_.feet[1];
        const bool finite=std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(dz);
        // A render report can carry the new ack with old interpolated/server
        // coordinates. Do not classify those as an escape before arrival.
        if(!arrived_&&finite&&dx*dx+dz*dz<=0.25&&dy>=-0.6&&dy<=1.0) arrived_=true;
        if(!arrived_) {
            groundSeen_=false;
        } else if(!finite||
            dx*dx+dz*dz>0.25||dy<-1.5||dy>2.5) {
            state_=State::failed; reason_="MC left the bounded settling area";
        } else if(mc->flags&kMcOnGround) {
            if(!groundSeen_) { groundSeen_=true; groundAt_=now; }
            if(now-groundAt_>=500) {
                state_=std::abs(dy)<=0.6?State::passed:State::failed;
                reason_=state_==State::passed?"released MC player settled near Witcher feet":
                    "MC settled at an unexpected height";
            }
        } else groundSeen_=false;
    }
    if(state_!=State::waiting) {
        sky.teleportSeq=teleport_;
        sky.posX=anchor_.feet[0]; sky.posY=anchor_.feet[1]+0.5; sky.posZ=anchor_.feet[2];
        if(state_==State::running) {
            sky.worldId=anchor_.worldId; sky.collisionEpoch=anchor_.collisionEpoch;
        }
        sky.flags=state_==State::running?kSkyInGame:kSkyMenuOpen;
    }
}
}
