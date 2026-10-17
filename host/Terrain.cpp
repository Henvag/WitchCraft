#include "Terrain.h"
#include "NativeLayout.h"
#include <algorithm>
#include <cmath>

namespace witchcraft {
bool TerrainSchedule::BeginIfNeeded(std::uint32_t world, std::uint32_t relocation,
    float x, float y, float z, std::uint64_t now,bool play) {
    const bool invalidated=!started_ || world_!=world || relocation_!=relocation;
    if (!invalidated && (!published_ || (patch_.Contains(x,y,play?6:4) &&
        std::abs(patch_.Height()-z)<=2 && now-publishedAt_<=(play?1000:5000)))) return false;
    patch_.Begin(x,y,z);
    world_=world; relocation_=relocation; started_=true; published_=false;
    return true;
}
void TerrainPatch::Begin(float x, float y, float z) {
    x_=std::floor(x)-cells/2; y_=std::floor(y)-cells/2; z_=z;
    points_={};
}
void TerrainPatch::QueryPoint(int index, float& x, float& y) const {
    if (index < corners) { x=x_+index%17; y=y_+index/17; }
    else { index-=corners; x=x_+index%16+0.5f; y=y_+index/16+0.5f; }
}
bool TerrainPatch::Contains(float x, float y,float margin) const {
    return std::isfinite(margin)&&margin>=0&&margin<=cells/2&&
        x>=x_+margin && x<=x_+cells-margin && y>=y_+margin && y<=y_+cells-margin;
}
skycraft::proto::ColRegion TerrainPatch::Region(std::uint32_t epoch, std::uint32_t count) const {
    return {static_cast<int>(x_),static_cast<int>(std::floor(z_-depth)),static_cast<int>(-y_-cells),
        static_cast<int>(x_+cells),static_cast<int>(std::ceil(z_+2)),static_cast<int>(-y_),epoch,count};
}
std::vector<skycraft::proto::ColTri> TerrainPatch::Triangles() const {
    using namespace skycraft::proto;
    std::vector<ColTri> out;
    out.reserve(cells*cells*2);
    auto valid=[](const GroundPoint& p) {
        return p.hit && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            std::isfinite(p.up) && p.up>=0.5f && p.up<=1.1f;
    };
    auto supported=[&](const GroundPoint& a,const GroundPoint& b,const GroundPoint& c) {
        if(!valid(a)||!valid(b)||!valid(c)) return false;
        const double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
        const double vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        const double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        const double length=std::sqrt(nx*nx+ny*ny+nz*nz);
        // Test the measured triangle's slope, rather than the entire square's
        // height range. A walkable diagonal slope can span over a metre across
        // its opposite corners without containing a cliff.
        return length>1e-6&&nz/length>=0.5;
    };
    auto add=[&](const GroundPoint& a,const GroundPoint& b,const GroundPoint& c) {
        ColTri tri{};
        const GroundPoint* vertices[]{&a,&b,&c};
        for (int i=0;i<3;++i) {
            const float source[]{vertices[i]->x,vertices[i]->y,vertices[i]->z};
            float converted[3]{}; native::ToMinecraft(source,converted);
            std::copy_n(converted,3,tri.v+3*i);
        }
        // Sampling does not prove an object's material or destructibility.
        tri.flags=kTriTerrain; // Height-field support, without diggable/material permission.
        out.push_back(tri);
    };
    for (int y=0;y<cells;++y) for (int x=0;x<cells;++x) {
        const auto& a=points_[y*17+x]; const auto& b=points_[y*17+x+1];
        const auto& c=points_[(y+1)*17+x]; const auto& d=points_[(y+1)*17+x+1];
        const auto& centre=points_[corners+y*16+x];
        if(!valid(centre)) continue;
        if(supported(a,b,c)&&supported(b,d,c)&&
            std::abs(centre.z-(b.z+c.z)*0.5f)<=0.15f) {
            add(a,b,c); add(b,d,c);
        } else {
            // Preserve each independently measured wedge. One missing or
            // discontinuous corner must not erase support on the opposite half
            // of the square; missing centres still leave real holes open.
            const GroundPoint* ring[]{&a,&b,&d,&c,&a};
            for(int edge=0;edge<4;++edge)
                if(supported(*ring[edge],*ring[edge+1],centre))
                    add(*ring[edge],*ring[edge+1],centre);
        }
    }
    return out;
}
}
