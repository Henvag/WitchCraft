#include "DiagnosticSettle.h"
#include <cstdio>
#include <cstdlib>
using namespace witchcraft;
using namespace skycraft::proto;
void Check(bool value) { if(!value) { puts("settle diagnostic check failed"); std::exit(1); } }
int main() {
    GameFrame game;
    game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
    game.worldId=1; game.collisionEpoch=1; game.relocation=1;
    game.feet[0]=100; game.feet[1]=10; game.feet[2]=-100;
    McState mc{}; mc.flags=kMcInWorld;
    SkyState sky{}; sky.flags=kSkyMenuOpen;
    DiagnosticSettle test;
    test.Update(&game,&mc,true,true,false,0,sky);
    test.Update(&game,&mc,true,true,false,3000,sky);
    Check(test.Status()==DiagnosticSettle::State::waiting&&sky.flags==kSkyMenuOpen);
    test.Update(&game,&mc,true,true,true,3001,sky);
    Check(test.Status()==DiagnosticSettle::State::running&&sky.flags==kSkyInGame&&sky.posY==10.5);
    const auto seq=sky.teleportSeq;
    mc.flags|=kMcOnGround; mc.x=100; mc.y=10; mc.z=-100;
    test.Update(&game,&mc,true,true,true,4000,sky);
    Check(test.Status()==DiagnosticSettle::State::running);
    mc.teleportAck=seq;
    mc.x=0.5; mc.y=8; mc.z=9.5;
    test.Update(&game,&mc,true,true,true,4500,sky);
    Check(test.Status()==DiagnosticSettle::State::running); // New ack with old position must wait.
    mc.x=100; mc.y=10; mc.z=-100;
    test.Update(&game,&mc,true,true,true,5000,sky);
    test.Update(&game,&mc,true,true,true,5500,sky);
    Check(test.Status()==DiagnosticSettle::State::passed&&sky.flags==kSkyMenuOpen);
    DiagnosticSettle fall;
    mc.flags=kMcInWorld; mc.teleportAck=0; sky.teleportSeq=0;
    fall.Update(&game,&mc,true,true,true,0,sky);
    fall.Update(&game,&mc,true,true,true,2000,sky);
    mc.teleportAck=sky.teleportSeq; mc.y=10.5;
    fall.Update(&game,&mc,true,true,true,2100,sky);
    mc.y=8;
    fall.Update(&game,&mc,true,true,true,2200,sky);
    Check(fall.Status()==DiagnosticSettle::State::failed&&sky.flags==kSkyMenuOpen);
    DiagnosticSettle moved;
    mc.y=10; mc.teleportAck=0; sky.teleportSeq=0;
    moved.Update(&game,&mc,true,true,true,0,sky);
    moved.Update(&game,&mc,true,true,true,2000,sky);
    game.feet[0]+=1;
    moved.Update(&game,&mc,true,true,true,2100,sky);
    Check(moved.Status()==DiagnosticSettle::State::failed);
    game.feet[0]-=1;
    DiagnosticSettle timeout;
    timeout.Update(&game,&mc,true,true,true,0,sky);
    timeout.Update(&game,&mc,true,true,true,2000,sky);
    timeout.Update(&game,&mc,true,true,true,17001,sky);
    Check(timeout.Status()==DiagnosticSettle::State::failed);
    DiagnosticSettle focus;
    focus.Update(&game,&mc,true,true,true,0,sky);
    focus.Update(&game,&mc,true,true,true,2000,sky);
    focus.Update(&game,&mc,true,false,true,2100,sky);
    Check(focus.Status()==DiagnosticSettle::State::failed&&sky.flags==kSkyMenuOpen);
    puts("settle wait, ack, success, falling, movement, timeout and focus handoff checks passed");
}
