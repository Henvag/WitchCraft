#include "PlaySession.h"
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <cstring>
using namespace witchcraft;
using namespace skycraft::proto;
void Check(bool value,int line) {if(!value) {std::printf("play check failed at %d\n",line);std::exit(1);}}
#define REQUIRE(value) Check((value),__LINE__)
struct Fixture {
    PlaySession play;GameFrame game{};McState mc{};SkyState sky{};
    Fixture() {
        game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;game.worldId=game.collisionEpoch=game.relocation=1;
        game.feet[1]=10;mc.flags=kMcInWorld|kMcOnGround;mc.curY=mc.y=10;
    }
    void Update(unsigned long long now,unsigned keys=0,bool stop=false,bool focus=true) {
        ++mc.frameCounter;sky.yaw=90;sky.viewportW=1920;
        play.Update(&game,&mc,true,focus,true,now,sky,keys,stop,true,true);
    }
    void Start() {Update(0);Update(2000);mc.teleportAck=sky.teleportSeq;Update(2100);Update(2600);REQUIRE(play.Active());}
    void Move(double x) {game.feet[0]=mc.x=mc.curX=x;}
};
int main() {
    Fixture flight;flight.Start();flight.mc.flags|=kMcCanFly;
    auto fly=[&](unsigned now,unsigned keys=0,bool stop=false,bool camera=true) {
        ++flight.mc.frameCounter;
        flight.play.Update(&flight.game,&flight.mc,true,true,true,now,flight.sky,keys,stop,true,true,camera);
    };
    fly(2700,16);REQUIRE(flight.play.Keys()==16);
    fly(2750);fly(2800,16);REQUIRE(flight.play.Keys()==16); // Vanilla double-tap edges.
    flight.mc.flags|=kMcFlying;flight.mc.flags&=~kMcOnGround;flight.mc.curY=30;
    fly(5000,17);REQUIRE(flight.play.Active()&&flight.play.Keys()==17);
    fly(5100);REQUIRE(!flight.play.Keys());
    flight.mc.curY=8;fly(5150);REQUIRE(flight.play.Active()); // Native ground may rise underneath flight.
    flight.mc.flags|=kMcScreenOpen;fly(5200,17);REQUIRE(flight.play.Active()&&!flight.play.Keys());
    fly(5300,0,true);REQUIRE(!flight.play.Active());
    Fixture gated;gated.Start();gated.mc.flags|=kMcFlying|kMcCanFly;gated.Update(2700);
    REQUIRE(!gated.play.Active()); // Flight requires the native camera hook.
    Fixture water;water.Start();water.mc.flags&=~kMcOnGround;water.mc.flags|=kMcInWater;
    water.mc.curY=10.3;water.Update(2700,17);water.Update(4700,17);
    REQUIRE(water.play.Active()&&water.play.Keys()==17);
    water.mc.flags|=kMcSwimming;water.Update(4800,1);REQUIRE(water.play.Active());
    water.mc.flags&=~(kMcInWater|kMcSwimming);water.Update(4900);water.Update(6501);
    REQUIRE(!water.play.Active()); // Leaving water restores ordinary fall bounds.
    Fixture deep;deep.Start();deep.mc.flags|=kMcInWater;deep.mc.curY=9;
    deep.Update(2700);REQUIRE(!deep.play.Active());
    Fixture f;f.Start();f.Update(2700,9);REQUIRE(f.play.Keys()==9);
    f.Move(3.5);f.Update(2800,9);REQUIRE(f.play.Active()); // No former 3 m diagnostic boundary.
    f.Update(30000,1);REQUIRE(f.play.Active()); // No 20-second diagnostic return.
    f.mc.flags|=kMcScreenOpen;f.Update(30100,31);REQUIRE(f.play.Active()&&!f.play.Keys()&&f.sky.flags==kSkyInGame);
    f.Update(31000);REQUIRE(f.play.Active());
    f.mc.flags&=~kMcScreenOpen;f.Update(31100,1);REQUIRE(f.play.Keys()==1);
    f.Update(31200,17);REQUIRE(f.play.Keys()==17);
    f.mc.curY=11.2;f.mc.flags&=~kMcOnGround;f.Move(4.2);f.Update(31250,17);
    REQUIRE(f.play.Active()&&f.play.Keys()==1);double feet[3]{};f.play.PilotFeet(f.mc,feet);REQUIRE(feet[1]==10&&feet[0]==4.2);
    f.game.feet[1]=10.07;f.Update(31300,1);f.play.PilotFeet(f.mc,feet);
    REQUIRE(f.play.Active()&&f.play.GroundFollow()&&feet[1]==10.07); // Ground rises under a moving jump.
    f.Update(31600,1);REQUIRE(f.play.Active());
    f.mc.curY=10;f.mc.flags|=kMcOnGround;f.Update(31800,1);REQUIRE(f.play.Active()&&f.play.Keys()==1);
    REQUIRE(f.play.GroundFollow()); // Native terrain height remains authoritative on landing.
    f.Update(32400,16);REQUIRE(f.play.Keys()==16); // A second jump after releasing Space.
    f.mc.curY=10.5;f.mc.flags&=~kMcOnGround;f.Update(32450,16);
    f.mc.curY=10;f.mc.flags|=kMcOnGround;f.Update(33000,16);f.Update(33100,16);REQUIRE(!f.play.Keys());
    f.mc.flags|=kMcScreenOpen;f.Update(33200,0,true);REQUIRE(!f.play.Active()&&f.sky.flags==kSkyMenuOpen);
    Fixture fall;fall.Start();fall.mc.flags&=~kMcOnGround;fall.Update(2700);fall.Update(4201);REQUIRE(!fall.play.Active());
    Fixture block;block.Start();block.Update(2700,16);block.mc.flags&=~kMcOnGround;
    block.mc.curY=11.2;block.Update(2800);block.mc.curY=11;block.mc.flags|=kMcOnGround;block.Update(3100);
    REQUIRE(block.play.Active());block.play.PilotFeet(block.mc,feet);REQUIRE(feet[1]==10);
    block.mc.flags|=kMcScreenOpen;block.Update(3200);REQUIRE(block.play.Active());block.mc.flags&=~kMcScreenOpen;
    block.Update(3300,16);block.mc.flags&=~kMcOnGround;block.mc.curY=12.2;block.Update(3400);
    block.mc.curY=10;block.mc.flags|=kMcOnGround;block.Update(3800);REQUIRE(block.play.Active()); // Jump off block.
    block.mc.curY=11;block.Update(3900);block.mc.flags&=~kMcOnGround;block.mc.curY=10.5;block.Update(4000);
    block.mc.curY=10;block.mc.flags|=kMcOnGround;block.Update(4400);REQUIRE(block.play.Active()); // Walk off block.
    block.Update(4500,0,true);REQUIRE(!block.play.Active());
    Fixture tall;tall.Start();tall.Update(2700,16);tall.mc.flags&=~kMcOnGround;tall.mc.curY=11;tall.Update(2800);
    tall.mc.curY=12.2;tall.mc.flags|=kMcOnGround;tall.Update(2900);REQUIRE(!tall.play.Active());
    REQUIRE(std::strstr(tall.play.Reason(),"support outside")!=nullptr); // Landing cannot overwrite the failure.
    Fixture regularLag;regularLag.Start();regularLag.mc.curX=.8;regularLag.Update(2700,1);REQUIRE(regularLag.play.Active()&&regularLag.play.Keys()==1);
    Fixture lag;lag.Start();lag.mc.curX=1.4;lag.Update(2700,1);REQUIRE(lag.play.Active()&&!lag.play.Keys());
    lag.game.feet[0]=1.2;lag.Update(2750,1);REQUIRE(lag.play.Active()&&lag.play.Keys()==1);
    Fixture lagTimeout;lagTimeout.Start();lagTimeout.mc.curX=1.4;lagTimeout.Update(2700,1);lagTimeout.Update(3701,1);REQUIRE(!lagTimeout.play.Active());
    Fixture hardLag;hardLag.Start();hardLag.mc.curX=2.01;hardLag.Update(2700,1);REQUIRE(!hardLag.play.Active());
    Fixture focus;focus.Start();focus.Update(2700,1,false,false);REQUIRE(!focus.play.Active());
    Fixture world;world.Start();++world.game.worldId;world.Update(2700);REQUIRE(!world.play.Active());
    Fixture patch;patch.Start();patch.game.flags&=~kGameDiagnosticPatch;patch.Update(2700,1);REQUIRE(patch.play.Active()&&!patch.play.Keys());
    patch.game.flags|=kGamePlayPatch;patch.Update(3300,1);REQUIRE(patch.play.Active()&&patch.play.Keys()==1);
    ++patch.mc.frameCounter;patch.play.Update(&patch.game,&patch.mc,true,true,false,3310,patch.sky,1,false,true,true);
    REQUIRE(patch.play.Active()&&patch.play.Keys()==1); // Valid old coverage remains usable while a refresh is in transit.
    patch.game.flags&=~kGamePlayPatch;patch.mc.flags|=kMcScreenOpen;patch.Update(3350,1);REQUIRE(patch.play.Active()&&!patch.play.Keys()&&patch.sky.flags==kSkyInGame);
    patch.game.flags|=kGamePlayPatch;patch.mc.flags&=~kMcScreenOpen;patch.Update(3360,1);REQUIRE(patch.play.Keys()==1);
    patch.game.flags&=~kGamePlayPatch;patch.Update(3400,1);patch.Update(8401,1);REQUIRE(!patch.play.Active());
    Fixture bad;bad.Start();bad.mc.curY=std::numeric_limits<double>::quiet_NaN();bad.Update(2700);REQUIRE(!bad.play.Active());
    Fixture stalled;stalled.Start();stalled.play.Update(&stalled.game,&stalled.mc,true,true,true,3701,stalled.sky,0,false,true,true);REQUIRE(!stalled.play.Active());
    Fixture timeout;timeout.Start();timeout.Update(2700,16);timeout.Update(3451);REQUIRE(!timeout.play.Active());
    Fixture held;held.Start();held.Update(2700,16);held.mc.curY=10.5;held.mc.flags&=~kMcOnGround;held.Update(2750,16);
    held.mc.curY=10;held.mc.flags|=kMcOnGround;held.Update(3300,16);held.Update(3400,16);REQUIRE(held.play.Active()&&!held.play.Keys());
}
