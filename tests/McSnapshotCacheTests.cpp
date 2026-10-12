#include "McSnapshotCache.h"
#include "PlaySession.h"
#include <cstdlib>
#include <cstdio>
using namespace witchcraft;
using namespace skycraft::proto;
void Require(bool value,int line) {if(!value) {std::printf("snapshot check failed: %d\n",line);std::exit(1);}}
#define REQUIRE(value) Require((value),__LINE__)
int main() {
    McSnapshotCache cache;McState snapshot{},out{},torn{};
    snapshot.flags=kMcInWorld|kMcScreenOpen;snapshot.frameCounter=30;
    snapshot.teleportAck=9;snapshot.curX=42;
    REQUIRE(!cache.Update(true,1,false,torn,0,out));
    REQUIRE(cache.Update(true,1,true,snapshot,10,out));
    torn.flags=0;torn.curX=-100;
    REQUIRE(cache.Update(true,1,false,torn,26,out));
    REQUIRE(out.flags==snapshot.flags&&out.curX==42&&out.frameCounter==30);
    REQUIRE(cache.Update(true,1,false,torn,110,out));
    REQUIRE(!cache.Update(true,1,false,torn,111,out));
    REQUIRE(!out.flags); // Expiry is not extended by fallback reads.
    REQUIRE(cache.Update(true,1,true,snapshot,120,out));
    REQUIRE(!cache.Update(false,1,false,torn,121,out));
    REQUIRE(cache.Update(true,1,true,snapshot,130,out));
    REQUIRE(!cache.Update(true,2,false,torn,131,out)); // Never cross peer identities.
    REQUIRE(cache.Update(true,2,true,snapshot,140,out));
    snapshot.flags=kMcInWorld;REQUIRE(cache.Update(true,2,true,snapshot,141,out));
    REQUIRE(!(out.flags&kMcScreenOpen)); // A coherent chat close takes effect immediately.
    snapshot.flags=kMcDead;REQUIRE(cache.Update(true,2,true,snapshot,142,out));
    REQUIRE(out.flags==kMcDead); // Death/world changes must not use older cached flags.
    REQUIRE(!cache.Update(true,2,false,torn,139,out));
    // Reproduce the recorded race while an integrated play session owns chat.
    PlaySession play;GameFrame game{};McState mc{};SkyState sky{};
    game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
    game.worldId=game.collisionEpoch=game.relocation=1;game.feet[1]=10;
    mc.flags=kMcInWorld|kMcOnGround;mc.curY=mc.y=10;
    for(auto time:{0ull,2000ull,2100ull,2600ull}) {
        ++mc.frameCounter;if(time>=2100) mc.teleportAck=sky.teleportSeq;
        play.Update(&game,&mc,true,true,true,time,sky,0,false,true,true);
    }
    REQUIRE(play.Active());
    mc.flags|=kMcScreenOpen;++mc.frameCounter;
    REQUIRE(cache.Update(true,3,true,mc,2700,out));
    play.Update(&game,&out,true,true,true,2700,sky,0,false,true,true);
    REQUIRE(cache.Update(true,3,false,torn,2717,out));
    play.Update(&game,&out,true,true,true,2717,sky,0,false,true,true);
    REQUIRE(play.Active());
    mc.flags&=~kMcScreenOpen;++mc.frameCounter;
    REQUIRE(cache.Update(true,3,true,mc,2734,out));
    play.Update(&game,&out,true,true,true,2734,sky,0,false,true,true);
    REQUIRE(play.Active());
}
