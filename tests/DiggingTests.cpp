#include "Digging.h"
#include <cstdio>
using namespace witchcraft::digging;
using namespace skycraft::proto;
static double Area(const ColTri& t) {
    const double ux=t.v[3]-t.v[0],uy=t.v[4]-t.v[1],uz=t.v[5]-t.v[2],vx=t.v[6]-t.v[0],vy=t.v[7]-t.v[1],vz=t.v[8]-t.v[2];
    const double x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx;return std::sqrt(x*x+y*y+z*z)/2;
}
int main() {
    const auto a=WorldHash(u"levels\\novigrad\\novigrad.w2w");
    if(!a||a!=WorldHash(u"Levels/Novigrad/NOVIGRAD.w2w")) return 1; // Case and separator independent.
    if(a==WorldHash(u"levels\\skellige\\skellige.w2w")) return 2;
    if(WorldHash(u"")||WorldHash(u"levels\\novigrad\\novigrad.w2l")||WorldHash(u"bad\x01path.w2w")) return 3;
    // A flat 2x2 m diggable square at height 10.25, cells (0..1, 10, 0..1).
    ColTri t1{{0,10.25f,0,2,10.25f,0,0,10.25f,2},DiggableTerrain()};
    ColTri t2{{2,10.25f,0,2,10.25f,2,0,10.25f,2},DiggableTerrain()};
    ColTri wall{{0,10,0,0,12,0,0,10,1},0};
    std::size_t cut=99;
    auto none=CutDug({t1,t2,wall},[](int,int,int) {return false;},&cut);
    if(cut!=0||none.size()!=3) return 4;
    // Dig cell (0, 10, 0): one of four square metres is gone, ghosts keep the original.
    auto dug=CutDug({t1,t2,wall},[](int x,int y,int z) {return x==0&&y==10&&z==0;},&cut);
    if(cut!=1) return 5; // Only t1 overlaps the dug cell.
    double solid=0;unsigned ghosts=0;bool wallKept=false;
    for(const auto& t:dug) {
        if(t.flags&kTriGhost) {++ghosts;if(Area(t)<1.99) return 6;continue;}
        if(!t.flags) {wallKept=true;continue;}
        if((t.flags&(kTriTerrain|kTriDiggable))!=(kTriTerrain|kTriDiggable)) return 7;
        solid+=Area(t);
    }
    if(ghosts!=1||!wallKept||std::abs(solid-3)>1e-3) return 9;
    // A non-diggable wall in a dug cell is never cut.
    auto walls=CutDug({wall},[](int,int,int) {return true;},&cut);
    if(cut||walls.size()!=1) return 10;
    std::puts("digging checks passed");
}
