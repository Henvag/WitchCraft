#include "DiagnosticMovement.h"
#include <cstdlib>
#include <cstdio>
#include <limits>
using namespace witchcraft;
using namespace skycraft::proto;
void CheckImpl(bool value,int line) {if(!value) {std::printf("movement check failed at line %d\n",line);std::exit(1);}}
#define Check(value) CheckImpl((value),__LINE__)
struct Fixture {
    DiagnosticMovement test;
    GameFrame game{};McState mc{};SkyState sky{};
    Fixture(bool jump=false):test(jump) {
        game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
        game.worldId=game.collisionEpoch=game.relocation=1;
        game.feet[0]=100;game.feet[1]=10;game.feet[2]=-100;
        mc.flags=kMcInWorld|kMcOnGround;
        mc.x=mc.curX=100;mc.y=mc.curY=10;mc.z=mc.curZ=-100;
    }
    void Update(std::uint64_t now,std::uint8_t keys=0,bool stop=false,bool ready=true,bool hidden=true,bool focus=true) {
        ++mc.frameCounter;sky.yaw=45;
        test.Update(&game,&mc,true,focus,true,now,sky,keys,stop,ready,hidden);
    }
    void Start() {
        Update(0);Update(2000);mc.teleportAck=sky.teleportSeq;Update(2100);Update(2600);
        Check(test.Status()==DiagnosticMovement::State::ready&&test.Keys()==0&&test.Active());
    }
    void Move(double dx,double dz) {mc.curX+=dx;mc.curZ+=dz;game.feet[0]=mc.curX;game.feet[2]=mc.curZ;}
};
int main() {
    Fixture walk;walk.Start();walk.Update(2700,1,false,false);Check(!walk.test.Keys());
    walk.Update(2750,1,false,true,false);Check(!walk.test.Keys());
    walk.Update(2800,1);walk.Move(0,.8);walk.Update(3100,1);Check(walk.test.Keys()==1); // Sustained beyond old 250 ms pulse.
    walk.Update(3200,0);Check(!walk.test.Keys()&&walk.test.Active());
    walk.Move(0,-.8);walk.Update(3400,4);Check(walk.test.Keys()==4);
    walk.Move(-.4,0);walk.Update(3600,2);Check(walk.test.Keys()==2);
    walk.Move(.4,0);walk.Update(3800,8);Check(walk.test.Keys()==8);
    walk.Move(-.2,.2);walk.Update(4000,3);Check(walk.test.Keys()==3);
    walk.Update(4100);walk.Update(4700);walk.Update(4800,0,true);
    Check(walk.test.Status()==DiagnosticMovement::State::passed&&!walk.test.Active()&&walk.sky.flags==kSkyMenuOpen&&walk.sky.yaw==45);
    walk.Update(4900,15);Check(!walk.test.Keys());
    Fixture radius;radius.Start();radius.Update(2700,1);radius.Move(0,2.4);radius.Update(2800,1);
    Check(radius.test.Status()==DiagnosticMovement::State::braking&&!radius.test.Keys());
    radius.Update(2900,1);radius.Update(3450,1);Check(radius.test.Status()==DiagnosticMovement::State::stopped);
    radius.Update(13500,1);Check(radius.test.Status()==DiagnosticMovement::State::passed&&!radius.test.Keys());
    Fixture area;area.Start();area.Move(3.1,0);area.Update(2700);Check(area.test.Status()==DiagnosticMovement::State::failed);
    Fixture lag;lag.Start();lag.mc.curX+=.8;lag.Update(2700,8);Check(lag.test.Status()==DiagnosticMovement::State::failed);
    Fixture focus;focus.Start();focus.Update(2700,1,false,true,true,false);Check(!focus.test.Active()&&!focus.test.Keys());
    Fixture lost;lost.Start();lost.Update(2700,1);lost.Update(2800,1,false,true,false);Check(!lost.test.Active());
    Fixture world;world.Start();++world.game.worldId;world.Update(2700);Check(!world.test.Active());
    Fixture fall;fall.Start();fall.mc.flags&=~kMcOnGround;fall.Update(2700);fall.Update(2951);Check(!fall.test.Active());
    Fixture invalid;invalid.Start();invalid.mc.curY=std::numeric_limits<double>::quiet_NaN();invalid.Update(2700);Check(!invalid.test.Active());
    Fixture cancel;cancel.Start();cancel.Update(2700,0,true);Check(cancel.test.Status()==DiagnosticMovement::State::cancelled);
    Fixture time;time.Start();time.Update(2700,1);time.Update(22700,1);Check(time.test.Status()==DiagnosticMovement::State::braking&&!time.test.Keys());
    Fixture path;path.Start();path.Update(2700,1);
    for(int i=0;i<8;++i) {path.Move(i%2?-1:1,0);path.Update(2800+i*100,2);}
    Check(path.test.Status()==DiagnosticMovement::State::braking&&!path.test.Keys());
    Fixture frozen;frozen.Start();frozen.test.Update(&frozen.game,&frozen.mc,true,true,true,3701,frozen.sky,1,false,true,true);Check(!frozen.test.Active());
    Fixture render;render.Start();render.test.Stop("render missing");render.Update(2700,15);Check(!render.test.Active()&&!render.test.Keys());
    Fixture jump(true);jump.Start();jump.Update(2700,15);Check(!jump.test.Keys());
    jump.Update(2750,16,false,true,false);Check(!jump.test.Keys());
    jump.Update(2800,16);Check(jump.test.Keys()==16);
    jump.mc.flags&=~kMcOnGround;
    for(int i=0;i<3;++i) {jump.mc.curY+=.4;jump.Update(2850+i*50,16);Check(jump.test.Active()&&!jump.test.Keys());}
    double pilotFeet[3]{};jump.test.PilotFeet(jump.mc,pilotFeet);Check(pilotFeet[1]==10&&jump.mc.curY>11);
    jump.Update(3200,16);Check(jump.test.Active()); // Airborne beyond walking guard, height above .6 m.
    jump.mc.curY=jump.game.feet[1]=10;jump.mc.flags|=kMcOnGround;
    jump.Update(3400,16);jump.Update(3900,16);Check(jump.test.Status()==DiagnosticMovement::State::stopped);
    jump.Update(4000,16);Check(!jump.test.Keys()); // No second jump while held.
    jump.Update(4100);jump.Update(14000);Check(jump.test.Active()); // No timed return after landing.
    jump.Update(14100,16);Check(jump.test.Keys()==16); // A new press starts another real jump.
    jump.mc.curY=10.42;jump.mc.flags&=~kMcOnGround;jump.Update(14150);
    jump.mc.curY=10;jump.mc.flags|=kMcOnGround;jump.Update(14500);jump.Update(15000);
    Check(jump.test.Status()==DiagnosticMovement::State::stopped);
    jump.Update(15100,0,true);Check(jump.test.Status()==DiagnosticMovement::State::passed);
    Fixture jumpFall(true);jumpFall.Start();jumpFall.mc.flags&=~kMcOnGround;jumpFall.Update(2700);Check(!jumpFall.test.Active());
    Fixture jumpCancel(true);jumpCancel.Start();jumpCancel.Update(2700,16);jumpCancel.Update(2750,0,true);Check(jumpCancel.test.Status()==DiagnosticMovement::State::cancelled&&!jumpCancel.test.Keys());
    Fixture anchorChanged(true);anchorChanged.Start();anchorChanged.Update(2700,16);anchorChanged.game.feet[1]+=.2;anchorChanged.Update(2750);Check(!anchorChanged.test.Active());
    Fixture planar(true);planar.Start();planar.Update(2700,16);planar.mc.curY+=1;planar.mc.curX+=.4;planar.game.feet[0]-=.4;planar.mc.flags&=~kMcOnGround;planar.Update(2750);Check(!planar.test.Active());
    walk.test.PilotFeet(walk.mc,pilotFeet);Check(pilotFeet[1]==walk.mc.curY);
}
