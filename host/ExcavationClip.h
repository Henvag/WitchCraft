#pragma once
#include "Excavation.h"
#include <algorithm>
#include <cmath>

namespace witchcraft::excavation {
using Position=std::array<double,3>;
struct Vertex { Position position{},weights{}; };
using Triangle=std::array<Vertex,3>;
struct CutResult {
    enum Status { complete,invalid,budgetExceeded } status=invalid;
    std::vector<Triangle> triangles;
};
namespace detail {
using Polygon=std::vector<Vertex>;
inline Vertex Interpolate(const Vertex& a,const Vertex& b,double fraction) {
    Vertex result;
    for(int axis=0;axis<3;++axis) {
        result.position[axis]=a.position[axis]+fraction*(b.position[axis]-a.position[axis]);
        result.weights[axis]=a.weights[axis]+fraction*(b.weights[axis]-a.weights[axis]);
    }
    return result;
}
// Disjoint half spaces except for a zero-area shared edge. Boundary-only
// polygons stay in the cut volume instead of becoming duplicate outside faces.
inline void Split(const Polygon& source,int axis,double plane,bool belowOutside,
    Polygon& outside,Polygon& inside) {
    outside.clear();inside.clear();
    for(std::size_t i=0;i<source.size();++i) {
        const auto& a=source[i]; const auto& b=source[(i+1)%source.size()];
        const double da=(a.position[axis]-plane)*(belowOutside?1:-1),
            db=(b.position[axis]-plane)*(belowOutside?1:-1);
        if(da<0) outside.push_back(a); else inside.push_back(a);
        if((da<0)!=(db<0)) {
            auto cross=Interpolate(a,b,da/(da-db));cross.position[axis]=plane;
            outside.push_back(cross);inside.push_back(cross);
        }
    }
}
inline double AreaSquared(const Triangle& t) {
    Position a{},b{},normal{};
    for(int axis=0;axis<3;++axis) {
        a[axis]=t[1].position[axis]-t[0].position[axis];
        b[axis]=t[2].position[axis]-t[0].position[axis];
    }
    for(int axis=0;axis<3;++axis) normal[axis]=a[(axis+1)%3]*b[(axis+2)%3]-a[(axis+2)%3]*b[(axis+1)%3];
    return normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
}
}

// Subtract Minecraft unit cells from a native triangle. Barycentric weights
// retain source UVs, normals and other interpolated attributes for a future
// native mesh backend. Caps bound CPU/memory work; failure returns no partial
// cut so a caller can retain its original geometry and collision together.
inline CutResult CutTriangle(const std::array<Position,3>& source,std::span<const Cell> cells,
    std::size_t maxTriangles=4096) {
    constexpr std::size_t maxCells=256,hardTriangleLimit=4096;
    constexpr double slop=1e-4; // A surface at integer Y belongs to the cell below.
    for(const auto& position:source) for(auto value:position)
        if(!std::isfinite(value)||std::abs(value)>1000000) return {};
    for(const auto& cell:cells) for(auto value:cell)
        if(value<-1000000||value>=1000000) return {};
    if(cells.size()>maxCells||!maxTriangles||maxTriangles>hardTriangleLimit)
        return {CutResult::budgetExceeded,{}};
    detail::Polygon initial;
    for(int corner=0;corner<3;++corner) {
        Vertex v{source[corner],{}};v.weights[corner]=1;initial.push_back(v);
    }
    if(detail::AreaSquared({initial[0],initial[1],initial[2]})<1e-18) return {CutResult::complete,{}};
    std::vector<detail::Polygon> pieces{std::move(initial)},next;
    for(const auto& cell:cells) {
        next.clear();
        for(const auto& piece:pieces) {
            bool intersects=true;
            for(int axis=0;axis<3;++axis) {
                double lo=piece[0].position[axis],hi=lo;
                for(const auto& v:piece) {lo=std::min(lo,v.position[axis]);hi=std::max(hi,v.position[axis]);}
                if(hi<cell[axis]-slop||lo>cell[axis]+1+slop) {intersects=false;break;}
            }
            if(!intersects) next.push_back(piece);
            else {
                auto remainder=piece;
                for(int axis=0;axis<3&&!remainder.empty();++axis) for(int side=0;side<2;++side) {
                    if(remainder.empty()) break;
                    detail::Polygon outside,inside;
                    detail::Split(remainder,axis,cell[axis]+(side?1+slop:-slop),!side,outside,inside);
                    if(outside.size()>=3) next.push_back(std::move(outside));
                    remainder=std::move(inside);
                }
            }
            if(next.size()>maxTriangles) return {CutResult::budgetExceeded,{}};
        }
        pieces=std::move(next);
        if(pieces.empty()) break;
    }
    CutResult result{CutResult::complete,{}};
    for(const auto& piece:pieces) for(std::size_t i=1;i+1<piece.size();++i) {
        Triangle triangle{piece[0],piece[i],piece[i+1]};
        if(detail::AreaSquared(triangle)<1e-18) continue;
        if(result.triangles.size()>=maxTriangles) return {CutResult::budgetExceeded,{}};
        result.triangles.push_back(triangle);
    }
    return result;
}
}
