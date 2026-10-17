#include "ViewDepth.h"
#include <cstdio>
using namespace witchcraft;
int main() {
    ViewDepthGrid grid;
    const float camera[]{0,0,2},forward[]{0,1,0};
    if(!grid.Begin(camera,forward,60,16.f/9)) return 1;
    if(grid.Begin(camera,forward,200,16.f/9)) return 2; // Invalid lens is rejected.
    grid.Begin(camera,forward,60,16.f/9);
    // A wall at Witcher Y = 10 across the whole view.
    for(int i=0;i<ViewDepthGrid::count;++i) {
        float from[3]{},to[3]{};grid.QueryRay(i,from,to);
        const float t=(10-from[1])/(to[1]-from[1]);
        const float hit[]{from[0]+(to[0]-from[0])*t,10,from[2]+(to[2]-from[2])*t};
        grid.Set(i,true,hit);
    }
    auto tris=grid.Triangles(0);
    if(tris.size()!=ViewDepthGrid::columns*ViewDepthGrid::rows*2) return 3;
    for(const auto& t:tris) for(int c=0;c<3;++c) if(std::abs(t.v[c*3+2]+10)>1e-3f) return 4; // MC Z = -Witcher Y.
    // Bias pushes each hit farther from the camera.
    auto biased=grid.Triangles(.5f);
    if(!(biased[0].v[2]<-10.2f)) return 5;
    // An object edge (depth step) is left open; a missed ray leaves its cells open.
    float from[3]{},to[3]{};grid.QueryRay(0,from,to);
    const float near[]{from[0]+(to[0]-from[0])*.01f,from[1]+(to[1]-from[1])*.01f,from[2]+(to[2]-from[2])*.01f};
    grid.Set(0,true,near);grid.Set(ViewDepthGrid::count-1,false,near);
    if(grid.Triangles(0).size()!=tris.size()-4) return 6;
    std::puts("view depth checks passed");
}
