#pragma once
#include "skycraft_protocol.h"
#include <vector>
#include <cmath>
namespace witchcraft {
inline std::vector<skycraft::proto::RenVertex> OcclusionVertices(
    const std::vector<skycraft::proto::ColTri>& triangles,double* origin) {
    std::vector<skycraft::proto::RenVertex> vertices;
    if(triangles.empty()) return vertices;
    for(const auto& triangle:triangles) {
        bool valid=true;
        for(float v:triangle.v) valid&=std::isfinite(v)&&std::abs(v)<=1000000;
        if(!valid) continue;
        const auto* a=triangle.v;double u[3]{},v[3]{};
        for(int i=0;i<3;++i) {u[i]=a[3+i]-a[i];v[i]=a[6+i]-a[i];}
        const double area=std::hypot(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);
        if(area<1e-6) continue;
        if(vertices.empty()) for(int i=0;i<3;++i) origin[i]=a[i];
        for(unsigned i=0;i<3;++i) {
            skycraft::proto::RenVertex vertex{};
            vertex.x=static_cast<float>(a[i*3]-origin[0]);
            vertex.y=static_cast<float>(a[i*3+1]-origin[1]);
            vertex.z=static_cast<float>(a[i*3+2]-origin[2]);vertices.push_back(vertex);
        }
    }
    return vertices;
}
}
