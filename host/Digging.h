#pragma once
#include "ExcavationClip.h"
#include "skycraft_protocol.h"
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

// Digging into Witcher terrain through SkyCraft's own mechanism: sampled land is
// sent as diggable triangles; Minecraft's SkyDig decides which cells are dug,
// reveals blocks around them and exports the hole walls, which the overlay draws
// over Witcher's image. The host removes dug cells from the collision and
// occlusion it derives from Witcher's surface, keeping each original triangle as
// a ghost so Minecraft still knows what is inside the ground.
namespace witchcraft::digging {
// A Witcher world file path ("levels\\novigrad\\novigrad.w2w") as an identity that
// survives game restarts, so holes saved with a Minecraft world reappear only in
// the Witcher world they were dug in. Zero means unavailable.
inline std::uint32_t WorldHash(std::u16string_view path) {
    if(path.size()<5||path.size()>512) return 0;
    auto lower=[](char16_t c) {return c>=u'A'&&c<=u'Z'?char16_t(c-u'A'+u'a'):c=='/'?u'\\':c;};
    const std::u16string_view suffix=u".w2w";
    for(std::size_t i=0;i<suffix.size();++i) if(lower(path[path.size()-suffix.size()+i])!=suffix[i]) return 0;
    std::uint32_t hash=2166136261u;
    for(const char16_t c:path) {
        if(c<0x20||c>0x7e) return 0; // Depot paths are plain ASCII.
        hash^=static_cast<std::uint32_t>(lower(c));hash*=16777619u;
    }
    return hash?hash:1;
}
inline constexpr std::uint32_t kGroundMaterial=skycraft::proto::kDigGrass;
inline std::uint32_t DiggableTerrain() {
    using namespace skycraft::proto;
    return kTriTerrain|kTriDiggable|(kGroundMaterial<<kTriMaterialShift);
}
// Cut dug cells out of diggable triangles. `dug(x, y, z)` takes Minecraft cells.
// A triangle that cannot be cut within budget is kept whole: collision then
// errs towards support rather than an unexplained hole.
template<class Dug>
std::vector<skycraft::proto::ColTri> CutDug(const std::vector<skycraft::proto::ColTri>& triangles,Dug&& dug,
    std::size_t* cutCount=nullptr) {
    using namespace skycraft::proto;
    std::vector<ColTri> out;out.reserve(triangles.size());
    std::size_t cut=0;
    std::vector<excavation::Cell> cells;
    for(const auto& tri:triangles) {
        if(!(tri.flags&kTriDiggable)||(tri.flags&kTriGhost)) {out.push_back(tri);continue;}
        int lo[3]{},hi[3]{};bool finite=true;
        for(int axis=0;axis<3;++axis) {
            const float a=tri.v[axis],b=tri.v[3+axis],c=tri.v[6+axis];
            if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)) {finite=false;break;}
            lo[axis]=static_cast<int>(std::floor(std::fmin(a,std::fmin(b,c))));
            hi[axis]=static_cast<int>(std::floor(std::fmax(a,std::fmax(b,c))));
        }
        cells.clear();
        if(finite&&(hi[0]-lo[0])<=8&&(hi[1]-lo[1])<=8&&(hi[2]-lo[2])<=8)
            for(int x=lo[0];x<=hi[0];++x) for(int y=lo[1];y<=hi[1];++y) for(int z=lo[2];z<=hi[2];++z)
                if(dug(x,y,z)) cells.push_back({x,y,z});
        if(cells.empty()) {out.push_back(tri);continue;}
        const std::array<excavation::Position,3> source{{{tri.v[0],tri.v[1],tri.v[2]},{tri.v[3],tri.v[4],tri.v[5]},{tri.v[6],tri.v[7],tri.v[8]}}};
        const auto result=excavation::CutTriangle(source,cells,256);
        if(result.status!=excavation::CutResult::complete) {out.push_back(tri);continue;}
        // Bounding boxes overlap more cells than the surface enters; an untouched
        // triangle stays whole rather than gaining a redundant ghost.
        auto area=[](const excavation::Position& a,const excavation::Position& b,const excavation::Position& c) {
            const double ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
            return std::sqrt(std::pow(uy*vz-uz*vy,2)+std::pow(uz*vx-ux*vz,2)+std::pow(ux*vy-uy*vx,2))/2;
        };
        double remaining=0;
        for(const auto& piece:result.triangles) remaining+=area(piece[0].position,piece[1].position,piece[2].position);
        const double whole=area(source[0],source[1],source[2]);
        if(remaining>=whole*(1-1e-6)-1e-9) {out.push_back(tri);continue;}
        ++cut;
        auto ghost=tri;ghost.flags|=kTriGhost;out.push_back(ghost);
        for(const auto& piece:result.triangles) {
            ColTri remaining=tri;
            for(int corner=0;corner<3;++corner) for(int axis=0;axis<3;++axis)
                remaining.v[corner*3+axis]=static_cast<float>(piece[corner].position[axis]);
            out.push_back(remaining);
        }
    }
    if(cutCount) *cutCount=cut;
    return out;
}
}
