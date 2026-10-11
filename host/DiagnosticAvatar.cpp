#include "DiagnosticAvatar.h"
#include <cmath>
namespace witchcraft {
void DiagnosticAvatar::Update(const GameFrame* game,const skycraft::proto::McState* mc,bool alive,bool focused,
    bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky) {
    using namespace skycraft::proto;
    visible_=false;
    if(!anchored_&&settle_.Status()==DiagnosticSettle::State::failed&&game&&mc&&alive&&focused&&consumed&&
        (game->flags&kGamePlaying)&&(game->flags&kGameDiagnosticPatch)&&!(game->flags&(kGameMenu|kGameLoading))&&
        (mc->flags&kMcInWorld)&&!(mc->flags&(kMcDead|kMcFlying|kMcScreenOpen))) {
        // Display-only mode may recover an interrupted settle after returning
        // focus. A fresh stationary wait and teleport ack are still required.
        settle_=DiagnosticSettle{};
    }
    if(sequence_) sky.teleportSeq=sequence_;
    settle_.Update(game,mc,alive,focused,consumed,now,sky);
    sequence_=sky.teleportSeq;
    reason_=settle_.Reason();
    if(settle_.Status()!=DiagnosticSettle::State::passed) return;
    if(!anchored_) { anchor_=*game; anchored_=true; }
    reason_="display hidden during menu, focus loss or unavailable frame";
    if(!game||!mc||!alive||!focused||!(game->flags&kGamePlaying)||
        !(game->flags&kGameDiagnosticPatch)||(game->flags&(kGameMenu|kGameLoading))||
        !(mc->flags&kMcInWorld)||(mc->flags&(kMcDead|kMcFlying|kMcScreenOpen))) return;
    reason_="display hidden after anchor or Minecraft position changed";
    if(game->worldId!=anchor_.worldId||game->collisionEpoch!=anchor_.collisionEpoch||game->relocation!=anchor_.relocation) return;
    double moved=0;for(int i=0;i<3;++i) moved+=std::pow(game->feet[i]-anchor_.feet[i],2);
    const double offset=std::hypot(mc->x-anchor_.feet[0],mc->z-anchor_.feet[2]),height=mc->y-anchor_.feet[1];
    if(!std::isfinite(moved)||moved>0.0625||!std::isfinite(offset)||offset>0.5||!std::isfinite(height)||std::abs(height)>0.6) return;
    reason_="waiting for Minecraft third-person avatar mesh";
    if(mc->cameraMode!=1) return;
    visible_=true; reason_="stationary avatar display using Witcher camera; Geralt control unchanged";
}
}
