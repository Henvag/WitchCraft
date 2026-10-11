#include "Walls.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace witchcraft;
void Check(bool value) { if (!value) { puts("wall check failed"); std::exit(1); } }
// A room's four planar walls at +/-4m, intersected independently of the mesh.
void FillRoom(WallPatch& patch) {
    patch.Begin(0,0,0);
    for (int i=0;i<WallPatch::samples;++i) {
        float from[3]{},to[3]{}; patch.QueryRay(i,from,to);
        const bool x=std::abs(to[0])>std::abs(to[1]);
        const int axis=x?0:1;
        const float sign=to[axis]>0?1.0f:-1.0f;
        const float t=4/std::abs(to[axis]);
        SurfacePoint p{{to[0]*t,to[1]*t,from[2]},{0,0,0},true};
        p.normal[axis]=-sign;
        Check(ValidSurfaceHit(from,to,p));
        patch.Set(i,p);
    }
}
int main() {
    const float from[]{0,0,0},to[]{8,0,0};
    SurfacePoint p{{4,0,0},{-1,0,0},true};
    Check(ValidSurfaceHit(from,to,p));
    p.position[1]=0.1f; Check(!ValidSurfaceHit(from,to,p));
    p.position[1]=0; p.position[0]=9; Check(!ValidSurfaceHit(from,to,p));
    p.position[0]=-1; Check(!ValidSurfaceHit(from,to,p));
    p.position[0]=4; p.normal[0]=2; Check(!ValidSurfaceHit(from,to,p));
    p.normal[0]=std::numeric_limits<float>::quiet_NaN(); Check(!ValidSurfaceHit(from,to,p));
    p.normal[0]=-1; Check(!ValidSurfaceHit(from,from,p));
    p.hit=false; Check(!ValidSurfaceHit(from,to,p));
    WallPatch walls; FillRoom(walls);
    const auto mesh=walls.Triangles();
    Check(!mesh.empty());
    for (const auto& tri:mesh) {
        Check(tri.flags==0);
        float ab[3]{},ac[3]{},cross[3]{};
        for (int i=0;i<3;++i) { ab[i]=tri.v[3+i]-tri.v[i]; ac[i]=tri.v[6+i]-tri.v[i]; }
        cross[0]=ab[1]*ac[2]-ab[2]*ac[1];
        cross[1]=ab[2]*ac[0]-ab[0]*ac[2];
        cross[2]=ab[0]*ac[1]-ab[1]*ac[0];
        const float cx=(tri.v[0]+tri.v[3]+tri.v[6])/3;
        const float cz=(tri.v[2]+tri.v[5]+tri.v[8])/3;
        Check(cross[0]*-cx+cross[2]*-cz>0); // Faces toward the room centre.
        Check(std::abs(cross[1])<0.00001f);
    }
    // Both sectors bordering angle zero exist, without a gap in the wraparound.
    bool negative=false,positive=false;
    for (const auto& tri:mesh) if (tri.v[0]>3.9f) {
        negative|=tri.v[2]<0; positive|=tri.v[2]>0;
    }
    Check(negative&&positive);
    const auto full=mesh.size();
    walls.Set(WallPatch::corners,{});
    Check(walls.Triangles().size()==full-2); // Missing centre leaves a hole.
    FillRoom(walls);
    float a[3]{},b[3]{}; walls.QueryRay(WallPatch::corners,a,b);
    SurfacePoint changed{{b[0]*0.75f,b[1]*0.75f,a[2]},{-1,0,0},true};
    walls.Set(WallPatch::corners,changed);
    Check(walls.Triangles().size()==full-2); // Different surface must not bridge.
    FillRoom(walls);
    changed={{b[0]*0.5f,b[1]*0.5f,a[2]},{1,0,0},true};
    walls.Set(WallPatch::corners,changed);
    Check(walls.Triangles().size()==full-2); // Back face cannot create a wall.
    puts("wall segment validation, winding, seams, holes and surface-change checks passed");
}
