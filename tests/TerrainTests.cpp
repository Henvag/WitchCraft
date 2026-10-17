#include "Terrain.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace witchcraft;
void Check(bool value) { if (!value) { puts("terrain check failed"); std::exit(1); } }
void Fill(TerrainPatch& patch) {
    patch.Begin(0,0,0);
    for (int i=0;i<TerrainPatch::samples;++i) {
        float x,y; patch.QueryPoint(i,x,y);
        patch.Set(i,{x,y,x*0.1f+y*0.05f,0.99f,true});
    }
}
double CornerCellArea(const std::vector<skycraft::proto::ColTri>& triangles) {
    double area=0;
    for(const auto& t:triangles) {
        const double x=(t.v[0]+t.v[3]+t.v[6])/3.0;
        const double y=-(t.v[2]+t.v[5]+t.v[8])/3.0;
        if(x< -8||x> -7||y< -8||y> -7) continue;
        area+=std::abs((t.v[3]-t.v[0])*(t.v[8]-t.v[2])-
            (t.v[5]-t.v[2])*(t.v[6]-t.v[0]))*.5;
    }
    return area;
}
int main() {
    TerrainSchedule stream;
    Check(stream.BeginIfNeeded(1,1,0,0,0,0));
    // Walking beyond the inner margin must not discard partial samples or a
    // finished packet waiting for queue space. Even refresh time cannot do so.
    stream.Patch().Set(0,{-8,-8,0,1,true});
    for (int step=1;step<=100;++step)
        Check(!stream.BeginIfNeeded(1,1,step*0.5f,0,step*0.1f,step*100));
    Check(stream.Patch().Region(1,0).minX==-8);
    stream.Published(10000);
    Check(stream.BeginIfNeeded(1,1,50,0,10,10001));
    Check(stream.Patch().Region(1,0).minX==42);
    stream.Published(11000);
    Check(!stream.BeginIfNeeded(1,1,50,0,10,16000));
    Check(stream.BeginIfNeeded(1,1,50,0,10,16001));
    // Teleports and new worlds discard even an unpublished old patch.
    Check(stream.BeginIfNeeded(1,2,100,0,0,16002));
    Check(stream.BeginIfNeeded(2,2,-100,0,0,16003));
    Check(stream.Patch().Region(2,0).minX==-108);
    stream.Published(16004);
    Check(stream.BeginIfNeeded(2,2,-100,0,3,16005));
    TerrainPatch patch;
    Fill(patch);
    auto tris=patch.Triangles();
    Check(tris.size()==512);
    for (const auto& t:tris) {
        const float ax=t.v[3]-t.v[0], az=t.v[5]-t.v[2];
        const float bx=t.v[6]-t.v[0], bz=t.v[8]-t.v[2];
        Check(az*bx-ax*bz>0); // Winding faces up after the axis transform.
        Check(t.flags==skycraft::proto::kTriTerrain); // Support must not authorize destruction.
    }
    const auto bounds=patch.Region(9,512);
    Check(bounds.epoch==9&&bounds.minX==-8&&bounds.maxX==8&&bounds.minZ==-8&&bounds.maxZ==8);
    Check(bounds.minY==-24&&bounds.maxY==2&&bounds.maxY-bounds.minY<=32);
    Check(patch.Contains(0,0)&&!patch.Contains(8,0));
    Check(patch.Contains(2,0,6)&&!patch.Contains(2.1f,0,6));
    Check(patch.Contains(6,0,2)&&!patch.Contains(6.1f,0,2));
    TerrainSchedule playSchedule;
    Check(playSchedule.BeginIfNeeded(1,1,0,0,0,0,true));
    playSchedule.Published(10);
    Check(!playSchedule.BeginIfNeeded(1,1,2,0,0,20,true));
    Check(playSchedule.BeginIfNeeded(1,1,3,0,0,30,true)); // Prefetch before old 4 m boundary.
    Check(!playSchedule.BeginIfNeeded(1,1,6,0,0,40,true)); // Finish a moving patch before restarting.
    playSchedule.Published(50);
    Check(playSchedule.BeginIfNeeded(1,1,6,0,0,60,true));
    // Missing centre hit must remove both triangles instead of bridging a hole.
    patch.Set(TerrainPatch::corners,{0,0,0,0,false});
    Check(patch.Triangles().size()==510);
    Check(CornerCellArea(patch.Triangles())==0);
    Fill(patch);
    float x,y; patch.QueryPoint(TerrainPatch::corners,x,y);
    patch.Set(TerrainPatch::corners,{x,y,-5,1,true});
    Check(patch.Triangles().size()==510); // Discontinuous lower surface.
    Check(CornerCellArea(patch.Triangles())==0);
    Fill(patch);
    patch.QueryPoint(TerrainPatch::corners,x,y);
    const float measured=x*.1f+y*.05f+.3f;
    patch.Set(TerrainPatch::corners,{x,y,measured,1,true});
    const auto uneven=patch.Triangles();
    Check(uneven.size()==514); // Measured bump retains support instead of a one-metre hole.
    for(int i=0;i<4;++i) Check(std::abs(uneven[i].v[7]-measured)<1e-5);
    Fill(patch);
    patch.QueryPoint(0,x,y); patch.Set(0,{x,y,100,1,true});
    Check(patch.Triangles().size()==512);
    Check(std::abs(CornerCellArea(patch.Triangles())-.5)<1e-6); // Opposite half survives; cliff cannot become a ramp.
    for(const auto& t:patch.Triangles()) for(int i=0;i<3;++i) Check(t.v[i*3+1]<2);
    Fill(patch);
    patch.Set(0,{x,y,0,std::numeric_limits<float>::infinity(),true});
    Check(patch.Triangles().size()==512);
    Check(std::abs(CornerCellArea(patch.Triangles())-.5)<1e-6);
    Fill(patch);
    patch.Set(0,{x,y,std::numeric_limits<float>::quiet_NaN(),1,true});
    Check(patch.Triangles().size()==512);
    Fill(patch);
    patch.Set(0,{});
    Check(std::abs(CornerCellArea(patch.Triangles())-.5)<1e-6);
    patch.Set(1,{});
    Check(std::abs(CornerCellArea(patch.Triangles())-.25)<1e-6);
    // A 49-degree diagonal slope spans 1.6 m across a square. Its measured
    // triangles are walkable and must not be discarded by a whole-cell limit.
    patch.Begin(0,0,0);
    for(int i=0;i<TerrainPatch::samples;++i) {
        patch.QueryPoint(i,x,y);
        patch.Set(i,{x,y,x*.8f+y*.8f,.662f,true});
    }
    Check(patch.Triangles().size()==512);
    Check(std::abs(CornerCellArea(patch.Triangles())-1)<1e-6);
    // A real missing centre still removes the surface even on this incline.
    patch.Set(TerrainPatch::corners,{});
    Check(CornerCellArea(patch.Triangles())==0);
    // Lake-bed samples below the former 8 m limit fit the advertised region.
    patch.Begin(0,0,0);
    for(int i=0;i<TerrainPatch::samples;++i) {
        patch.QueryPoint(i,x,y);patch.Set(i,{x,y,-20,1,true});
    }
    Check(patch.Triangles().size()==512&&patch.Region(9,512).minY<=-20);
    puts("terrain slope, partial support, winding, real holes, cliffs and depth checks passed");
}
