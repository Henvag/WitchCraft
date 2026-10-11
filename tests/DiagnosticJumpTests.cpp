#include "DiagnosticJump.h"
#include <cstdlib>
#include <initializer_list>
#include <limits>
using namespace witchcraft;
void Check(bool b) {if(!b) std::exit(1);}
int main() {
    DiagnosticJump jump;
    jump.Update(0,false,true,0,0);Check(!jump.Pulse(0));
    jump.Update(100,true,true,0,0);Check(jump.Pulse(100)&&jump.Pulse(199)&&!jump.Pulse(200));
    jump.Update(150,true,false,.42,0);Check(!jump.Pulse(150));
    jump.Update(400,true,false,1.25,0);Check(jump.Running()&&jump.Peak()==1.25);
    jump.Update(800,true,true,.01,0);Check(jump.Status()==DiagnosticJump::State::landing);
    jump.Update(1300,true,true,.01,0);Check(jump.Status()==DiagnosticJump::State::passed);
    jump.Update(1400,true,true,.01,0);Check(!jump.Pulse(1400)&&!jump.Running());
    jump.Update(1450,false,true,.01,0);jump.Update(1500,true,true,.01,0);Check(jump.Pulse(1500));
    jump.Update(1550,false,false,.42,0);jump.Update(1800,false,true,.01,0);jump.Update(2300,false,true,.01,0);
    Check(jump.Status()==DiagnosticJump::State::passed);
    DiagnosticJump fall;fall.Update(0,false,false,0,0);Check(fall.Status()==DiagnosticJump::State::failed);
    DiagnosticJump noTakeoff;noTakeoff.Update(0,true,true,0,0);noTakeoff.Update(751,true,true,0,0);Check(noTakeoff.Status()==DiagnosticJump::State::failed);
    DiagnosticJump floating;floating.Update(0,true,true,0,0);floating.Update(100,false,false,.42,0);floating.Update(3001,false,false,.5,0);Check(floating.Status()==DiagnosticJump::State::failed);
    DiagnosticJump tiny;tiny.Update(0,true,true,0,0);tiny.Update(50,false,false,.1,0);tiny.Update(100,false,true,0,0);Check(tiny.Status()==DiagnosticJump::State::failed);
    DiagnosticJump ledge;ledge.Update(0,true,true,0,0);ledge.Update(50,false,false,.42,0);ledge.Update(100,false,true,.3,0);Check(ledge.Status()==DiagnosticJump::State::failed);
    for(auto height:{1.61,-.26,std::numeric_limits<double>::quiet_NaN()}) {DiagnosticJump bad;bad.Update(0,true,true,height,0);Check(bad.Status()==DiagnosticJump::State::failed);}
    DiagnosticJump sideways;sideways.Update(0,true,true,0,.41);Check(sideways.Status()==DiagnosticJump::State::failed);
    jump.Update(2400,false,false,0,0);Check(jump.Status()==DiagnosticJump::State::failed);
}
