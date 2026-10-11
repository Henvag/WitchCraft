#include "RenderTiming.h"
#include <cstdlib>
#include <limits>
using namespace witchcraft;
using namespace skycraft::proto;
int main() {
    // A 60 Hz game with sub-ms jitter must not lose alternate observations.
    ObserverCadence cadence;
    for(int frame=0;frame<600;++frame) {
        const std::int64_t at=1000000+frame*1000000LL/60+(frame%2?400:0);
        if(!cadence.Due(at,1000000)||cadence.Due(at+100,1000000)) return 8;
    }
    if(!cadence.Due(20000000,1000000)||cadence.Due(20000100,1000000)) return 9;
    ObserverCadence fast;unsigned accepted=0;
    for(int frame=0;frame<240;++frame) accepted+=fast.Due(frame*1000000LL/120,1000000);
    if(accepted!=120) return 10;
    ObserverCadence slow;
    for(int frame=0;frame<30;++frame) if(!slow.Due(frame*1000000LL/30,1000000)) return 11;
    McState mc{};mc.tickQpc=1000;mc.tickMs=50;
    mc.prevX=10;mc.curX=11;mc.prevY=mc.curY=3;mc.prevZ=4;mc.curZ=5;
    mc.x=10.3;mc.y=3;mc.z=4.3;
    auto first=RenderMcState(mc,1000,1000),half=RenderMcState(mc,1025,1000),last=RenderMcState(mc,1050,1000);
    if(first.x!=10||half.x!=10.5||half.z!=4.5||last.x!=11||mc.x!=10.3) return 1;
    if(RenderMcState(mc,1075,1000).x!=11||RenderMcState(mc,999,1000).x!=10) return 2;
    if(RenderMcState(mc,1101,1000).x!=mc.x||RenderMcState(mc,1025,0).x!=mc.x) return 3;
    mc.flags=kMcScreenOpen;if(RenderMcState(mc,1025,1000).x!=mc.x) return 4;
    mc.flags=0;mc.curX=20;if(RenderMcState(mc,1025,1000).x!=mc.x) return 5;
    mc.curX=std::numeric_limits<double>::quiet_NaN();if(RenderMcState(mc,1025,1000).x!=mc.x) return 6;
    mc.curX=11;mc.tickQpc=0;if(RenderMcState(mc,1025,1000).x!=mc.x) return 7;
}
