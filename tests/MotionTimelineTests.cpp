#include "MotionTimeline.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
using namespace witchcraft;
using namespace skycraft::proto;
McState Tick(int index,std::int64_t stamp) {
    McState mc{};mc.flags=kMcInWorld|kMcOnGround;mc.tickMs=50;
    mc.tickQpc=stamp;mc.frameCounter=index+1;mc.teleportAck=1;
    mc.prevX=index*.2;mc.curX=(index+1)*.2;mc.x=mc.curX;
    return mc;
}
int main() {
    constexpr std::int64_t frequency=1000000;
    MotionTimeline timeline;
    auto first=Tick(0,1000000);
    const auto a=timeline.Render(first,1048000,frequency);
    const auto b=timeline.Render(first,1053000,frequency);
    // A slightly late second tick no longer causes the first segment to hold
    // at its endpoint. Movement continues while remaining in known geometry.
    assert(b.x>a.x&&b.x<first.curX);
    const auto second=Tick(1,1057000);
    const auto c=timeline.Render(second,1060000,frequency);
    assert(c.x>=b.x&&c.x<=second.curX);
    const auto d=timeline.Render(first,1061000,frequency);
    assert(d.x>=c.x); // An old render snapshot cannot rewind native history.
    const auto stop=Tick(2,1100000);
    auto stationary=stop;stationary.prevX=stationary.curX=second.curX;
    timeline.Render(stationary,1110000,frequency);
    const auto held=timeline.Render(stationary,1300000,frequency,20);
    assert(std::abs(held.x-second.curX)<1e-9); // Never predict past a stop.
    auto teleport=Tick(3,1150000);teleport.teleportAck=2;
    teleport.prevX=100;teleport.curX=100.2;
    const auto relocated=timeline.Render(teleport,1160000,frequency);
    assert(relocated.x>=100&&relocated.x<=100.2);
    auto gui=teleport;gui.flags|=kMcScreenOpen;gui.x=101;
    assert(timeline.Render(gui,1170000,frequency).x==101);
    auto invalid=teleport;invalid.tickMs=std::nanf("");invalid.x=102;
    assert(timeline.Render(invalid,1180000,frequency).x==102);
    // Missing ticks interpolate only between received endpoints, not velocity.
    timeline.Reset();timeline.Render(Tick(0,1000000),1020000,frequency);
    const auto skipped=Tick(3,1150000);
    const auto gap=timeline.Render(skipped,1160000,frequency);
    assert(gap.x>=.2&&gap.x<=skipped.curX);
}
