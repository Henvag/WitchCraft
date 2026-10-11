#include "DiagnosticAvatar.h"
#include <cstdio>
#include <cstdlib>
using namespace witchcraft; using namespace skycraft::proto;
void Check(bool v) {if(!v){puts("avatar diagnostic check failed");std::exit(1);}}
int main() {
    GameFrame game{};game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
    game.worldId=game.collisionEpoch=game.relocation=1;
    game.feet[0]=100;game.feet[1]=10;game.feet[2]=-100;
    McState mc{};mc.flags=kMcInWorld|kMcOnGround;mc.x=100;mc.y=10;mc.z=-100;
    SkyState sky{}; DiagnosticAvatar avatar;
    auto update=[&](std::uint64_t now,bool focused=true,bool alive=true){avatar.Update(&game,&mc,alive,focused,true,now,sky);};
    update(0);update(2000);Check(!avatar.Visible());mc.teleportAck=sky.teleportSeq;
    update(2100);update(2600);Check(!avatar.Visible()); // Need MC third person.
    mc.cameraMode=1;update(2700);Check(avatar.Visible()&&sky.flags==kSkyMenuOpen);
    const auto sequence=sky.teleportSeq;
    game.camera[0]+=3;game.forward[0]=1;game.forward[2]=0;update(2800);Check(avatar.Visible());
    update(2900,false);Check(!avatar.Visible());update(3000);Check(avatar.Visible());
    game.flags|=kGameMenu;update(3100);Check(!avatar.Visible());game.flags&=~kGameMenu;
    update(3200);Check(avatar.Visible());
    update(3300,true,false);Check(!avatar.Visible());update(3400);Check(avatar.Visible());
    mc.x+=1;update(3500);Check(!avatar.Visible());mc.x-=1;
    game.feet[0]+=0.3;update(3600);Check(!avatar.Visible());game.feet[0]-=0.3;
    ++game.worldId;update(3700);Check(!avatar.Visible());--game.worldId;
    mc.flags|=kMcScreenOpen;update(3800);Check(!avatar.Visible());mc.flags&=~kMcScreenOpen;
    update(3900);Check(avatar.Visible()&&sky.teleportSeq==sequence&&sky.flags==kSkyMenuOpen);
    DiagnosticAvatar retry;sky={};mc.teleportAck=0;mc.cameraMode=1;
    retry.Update(&game,&mc,true,true,true,0,sky);
    retry.Update(&game,&mc,true,true,true,2000,sky);
    retry.Update(&game,&mc,true,false,true,2100,sky);Check(!retry.Visible());
    const auto abortedSeq=sky.teleportSeq;
    sky.teleportSeq=0; // Session's inactive frame must not reuse the aborted seq.
    retry.Update(&game,&mc,true,true,true,2200,sky);
    retry.Update(&game,&mc,true,true,true,4200,sky);Check(!retry.Visible()&&sky.teleportSeq!=abortedSeq);
    mc.teleportAck=sky.teleportSeq;
    retry.Update(&game,&mc,true,true,true,4300,sky);
    retry.Update(&game,&mc,true,true,true,4800,sky);Check(retry.Visible());
    puts("avatar settle gate, perspective, camera rotation, menu, focus, stale link and anchor checks passed");
}
