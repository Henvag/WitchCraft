#include "DiagnosticWalk.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace witchcraft;
using namespace skycraft::proto;
void Check(bool v) { if(!v) { puts("walk diagnostic check failed"); std::exit(1); } }
struct Fixture {
    DiagnosticWalk test;
    GameFrame game{};
    McState mc{};
    SkyState sky{};
    Fixture(bool follow=false,bool manual=false):test(follow,manual) {
        game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
        game.worldId=game.relocation=game.collisionEpoch=1;
        game.feet[0]=100; game.feet[1]=10; game.feet[2]=-100;
        mc.flags=kMcInWorld|kMcOnGround;
        mc.x=mc.curX=100; mc.y=mc.curY=10; mc.z=mc.curZ=-100;
    }
    void Update(std::uint64_t now,bool focus=true,bool alive=true,bool consumed=true) {
        ++mc.frameCounter;
        test.Update(&game,&mc,alive,focus,consumed,now,sky);
    }
    void Start() {
        Update(0); Update(2000);
        Check(!test.ForwardDown()); // No walking before gravity and ack.
        mc.teleportAck=sky.teleportSeq;
        Update(2100); Update(2600);
        Check(test.Status()==DiagnosticWalk::State::walking&&test.ForwardDown()&&sky.flags==kSkyInGame);
    }
};
int main() {
    Fixture success; success.Start(); const auto seq=success.sky.teleportSeq;
    success.mc.curZ+=0.8; success.Update(2850);
    Check(!success.test.ForwardDown()&&success.test.Status()==DiagnosticWalk::State::braking);
    success.mc.curZ+=0.1; success.Update(3850);
    Check(success.test.Status()==DiagnosticWalk::State::passed&&success.sky.flags==kSkyMenuOpen);
    success.Update(3950); Check(!success.test.ForwardDown()&&success.sky.teleportSeq==seq);
    Fixture focus; focus.Start(); focus.Update(2700,false);
    Check(focus.test.Status()==DiagnosticWalk::State::failed&&!focus.test.ForwardDown());
    Fixture stale; stale.Start(); stale.Update(2700,true,false);
    Check(stale.test.Status()==DiagnosticWalk::State::failed&&!stale.test.ForwardDown());
    Fixture frozen; frozen.Start();
    frozen.test.Update(&frozen.game,&frozen.mc,true,true,true,3601,frozen.sky);
    Check(frozen.test.Status()==DiagnosticWalk::State::failed&&!frozen.test.ForwardDown());
    Fixture fall; fall.Start(); fall.mc.flags&=~kMcOnGround; fall.Update(2700); fall.Update(2951);
    Check(fall.test.Status()==DiagnosticWalk::State::failed&&!fall.test.ForwardDown());
    Fixture escape; escape.Start(); escape.mc.curX+=2.1; escape.Update(2700);
    Check(escape.test.Status()==DiagnosticWalk::State::failed&&!escape.test.ForwardDown());
    Fixture moved; moved.Start(); moved.game.feet[0]+=0.3; moved.Update(2700);
    Check(moved.test.Status()==DiagnosticWalk::State::failed&&!moved.test.ForwardDown());
    Fixture world; world.Start(); ++world.game.worldId; world.Update(2700);
    Check(world.test.Status()==DiagnosticWalk::State::failed&&world.sky.worldId==1&&!world.test.ForwardDown());
    Fixture blocked; blocked.Start(); blocked.Update(2850); blocked.Update(3850);
    Check(blocked.test.Status()==DiagnosticWalk::State::failed&&!blocked.test.ForwardDown());
    Fixture noStop; noStop.Start(); noStop.mc.curZ+=0.7; noStop.Update(2850);
    noStop.mc.curZ+=0.4; noStop.Update(3850);
    Check(noStop.test.Status()==DiagnosticWalk::State::failed);
    Fixture nan; nan.Start(); nan.mc.curY=std::numeric_limits<double>::quiet_NaN(); nan.Update(2700);
    Check(nan.test.Status()==DiagnosticWalk::State::failed&&!nan.test.ForwardDown());
    Fixture preFail; preFail.Update(0); preFail.Update(2000); preFail.Update(2100,false);
    const auto failedSeq=preFail.sky.teleportSeq; preFail.Update(2200);
    Check(preFail.test.Status()==DiagnosticWalk::State::failed&&!preFail.test.ForwardDown()&&preFail.sky.teleportSeq==failedSeq);
    Fixture delivery; delivery.Update(0); delivery.Update(3000,true,true,false);
    Check(delivery.test.Status()==DiagnosticWalk::State::settling&&!delivery.test.ForwardDown());
    Fixture oldRaw; oldRaw.Update(0); oldRaw.Update(2000); oldRaw.mc.teleportAck=oldRaw.sky.teleportSeq;
    oldRaw.Update(2100); oldRaw.mc.curX=0; oldRaw.Update(2600);
    Check(oldRaw.test.Status()==DiagnosticWalk::State::failed&&!oldRaw.test.ForwardDown());
    Fixture heading; heading.sky.yaw=90; heading.Start(); heading.mc.curX-=0.8; heading.Update(2850); heading.Update(3850);
    Check(heading.test.Status()==DiagnosticWalk::State::passed);
    Fixture following(true);following.Start();following.mc.curZ+=0.8;following.game.feet[2]+=0.8;
    following.Update(2850);following.Update(3850);Check(following.test.Status()==DiagnosticWalk::State::passed);
    Fixture lag(true);lag.Start();lag.mc.curZ+=0.8;lag.Update(2700);
    Check(lag.test.Status()==DiagnosticWalk::State::failed&&!lag.test.ForwardDown());
    Fixture retry(true);retry.Update(0);retry.Update(2000);retry.Update(2100,false);
    Check(retry.test.Status()==DiagnosticWalk::State::failed);
    const auto interrupted=retry.sky.teleportSeq;retry.sky.teleportSeq=0;
    retry.Update(2200);retry.Update(4200);Check(retry.sky.teleportSeq!=interrupted);
    retry.mc.teleportAck=retry.sky.teleportSeq;retry.Update(4300);retry.Update(4800);
    Check(retry.test.ForwardDown());
    Fixture manual(true,true);
    manual.Update(0);manual.Update(2000);manual.mc.teleportAck=manual.sky.teleportSeq;
    manual.Update(2100);manual.Update(2600);
    Check(manual.test.Status()==DiagnosticWalk::State::ready&&!manual.test.ForwardDown()&&manual.test.Active());
    auto input=[&](Fixture& f,std::uint64_t now,bool forward=false,bool stop=false,bool ready=true,bool hidden=true) {
        ++f.mc.frameCounter;
        f.test.Update(&f.game,&f.mc,true,true,true,now,f.sky,forward,stop,ready,hidden);
    };
    input(manual,2700,true,false,false);Check(!manual.test.ForwardDown());
    input(manual,2800,true,false,true,false);Check(!manual.test.ForwardDown());
    input(manual,3000);Check(manual.test.Status()==DiagnosticWalk::State::ready);
    input(manual,3100,true);Check(manual.test.ForwardDown());
    manual.mc.curZ+=0.8;manual.game.feet[2]+=0.8;
    input(manual,3350,true);Check(manual.test.Status()==DiagnosticWalk::State::braking&&!manual.test.ForwardDown());
    input(manual,4350,true);Check(manual.test.Status()==DiagnosticWalk::State::stopped&&manual.test.Active());
    input(manual,4450,true);Check(!manual.test.ForwardDown()); // Holding/repeating W cannot restart.
    input(manual,4500,false,true);Check(manual.test.Status()==DiagnosticWalk::State::passed&&!manual.test.Active()&&manual.sky.flags==kSkyMenuOpen);
    Fixture cancel(true,true);cancel.Update(0);cancel.Update(2000);cancel.mc.teleportAck=cancel.sky.teleportSeq;cancel.Update(2100);cancel.Update(2600);
    input(cancel,2700,false,true);Check(cancel.test.Status()==DiagnosticWalk::State::cancelled&&!cancel.test.Active());
    Fixture noInput(true,true);noInput.Update(0);noInput.Update(2000);noInput.mc.teleportAck=noInput.sky.teleportSeq;noInput.Update(2100);noInput.Update(2600);
    input(noInput,32601);Check(noInput.test.Status()==DiagnosticWalk::State::cancelled);
    Fixture release(true,true);release.Update(0);release.Update(2000);release.mc.teleportAck=release.sky.teleportSeq;release.Update(2100);release.Update(2600);
    input(release,2700,true);input(release,2800,false);Check(release.test.Status()==DiagnosticWalk::State::braking&&!release.test.ForwardDown());
    Fixture hookFailure(true,true);hookFailure.Update(0);hookFailure.Update(2000);hookFailure.mc.teleportAck=hookFailure.sky.teleportSeq;hookFailure.Update(2100);hookFailure.Update(2600);
    input(hookFailure,2700,true);input(hookFailure,2750,true,false,false);Check(hookFailure.test.Status()==DiagnosticWalk::State::failed&&!hookFailure.test.ForwardDown());
    manual.test.Stop("render unavailable");input(manual,4600,true);Check(manual.test.Status()==DiagnosticWalk::State::failed&&!manual.test.Active());
    Fixture autoReturn(true,true);autoReturn.Update(0);autoReturn.Update(2000);autoReturn.mc.teleportAck=autoReturn.sky.teleportSeq;autoReturn.Update(2100);autoReturn.Update(2600);
    input(autoReturn,2700,true);autoReturn.mc.curZ+=0.8;autoReturn.game.feet[2]+=0.8;
    input(autoReturn,2950,true);input(autoReturn,3950);Check(autoReturn.test.Status()==DiagnosticWalk::State::stopped);
    input(autoReturn,18950);Check(autoReturn.test.Status()==DiagnosticWalk::State::passed&&!autoReturn.test.Active());
    Fixture lostHide(true,true);lostHide.Update(0);lostHide.Update(2000);lostHide.mc.teleportAck=lostHide.sky.teleportSeq;lostHide.Update(2100);lostHide.Update(2600);
    input(lostHide,2700,true);input(lostHide,2750,true,false,true,false);Check(lostHide.test.Status()==DiagnosticWalk::State::failed&&!lostHide.test.Active());
    puts("walk ack, release, bounds, ground, focus, stale reports, movement, world and stop checks passed");
}
