#include "TerrainVoxels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
namespace witchcraft {
std::vector<skycraft::proto::ColBlock> TerrainVoxels(const skycraft::proto::ColRegion& bounds,
    const std::vector<skycraft::proto::ColTri>& triangles) {
    using namespace skycraft::proto;
    std::map<std::array<int,3>,ColBlock> blocks;
    for(const auto& t:triangles) {
        if(!(t.flags&kTriTerrain)) continue;
        const double ax=t.v[0],az=t.v[2],bx=t.v[3],bz=t.v[5],cx=t.v[6],cz=t.v[8];
        const double det=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);
        if(!std::isfinite(det)||std::abs(det)<1e-8) continue;
        const int x0=std::max(bounds.minX*8,static_cast<int>(std::floor(std::min({ax,bx,cx})*8)));
        const int x1=std::min((bounds.maxX+1)*8-1,static_cast<int>(std::ceil(std::max({ax,bx,cx})*8))-1);
        const int z0=std::max(bounds.minZ*8,static_cast<int>(std::floor(std::min({az,bz,cz})*8)));
        const int z1=std::min((bounds.maxZ+1)*8-1,static_cast<int>(std::ceil(std::max({az,bz,cz})*8))-1);
        for(int x=x0;x<=x1;++x) for(int z=z0;z<=z1;++z) {
            const double px=(x+.5)/8,pz=(z+.5)/8;
            const double u=((bz-cz)*(px-cx)+(cx-bx)*(pz-cz))/det;
            const double v=((cz-az)*(px-cx)+(ax-cx)*(pz-cz))/det;
            if(u< -1e-6||v< -1e-6||u+v>1.000001) continue;
            const double height=u*t.v[1]+v*t.v[4]+(1-u-v)*t.v[7];
            // Fill two metres beneath a measured surface. Never fill across
            // unsampled holes or infer that deeper caves are solid.
            const int top=std::min((bounds.maxY+1)*8-1,static_cast<int>(std::ceil(height*8))-1);
            const int bottom=std::max(bounds.minY*8,top-15);
            const int blockX=static_cast<int>(std::floor(x/8.0)),blockZ=static_cast<int>(std::floor(z/8.0));
            for(int y=bottom;y<=top;++y) {
                const int blockY=static_cast<int>(std::floor(y/8.0));
                const std::array key{blockX,blockY,blockZ};
                auto [it,inserted]=blocks.try_emplace(key);
                auto& block=it->second;
                if(inserted) {block.x=blockX;block.y=blockY;block.z=blockZ;}
                block.bits[y-blockY*8]|=std::uint64_t{1}<<((z-blockZ*8)*8+x-blockX*8);
            }
        }
    }
    std::vector<ColBlock> result;result.reserve(blocks.size());
    for(const auto& [key,block]:blocks) result.push_back(block);
    return result;
}
}
