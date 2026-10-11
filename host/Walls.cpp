#include "Walls.h"
#include "NativeLayout.h"
#include <algorithm>
#include <cmath>

namespace witchcraft {
namespace {
float Dot(const float* a,const float* b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
bool Finite(const float* a) {
    return std::isfinite(a[0])&&std::isfinite(a[1])&&std::isfinite(a[2]);
}
float Distance(const SurfacePoint& a,const SurfacePoint& b) {
    return std::hypot(a.position[0]-b.position[0],a.position[1]-b.position[1],a.position[2]-b.position[2]);
}
}
bool ValidSurfaceHit(const float* from,const float* to,const SurfacePoint& hit) {
    if (!hit.hit||!Finite(from)||!Finite(to)||!Finite(hit.position)||!Finite(hit.normal)) return false;
    float ray[3]{},delta[3]{};
    for (int i=0;i<3;++i) { ray[i]=to[i]-from[i]; delta[i]=hit.position[i]-from[i]; }
    const float length2=Dot(ray,ray),normal2=Dot(hit.normal,hit.normal);
    if (!std::isfinite(length2)||length2<0.0001f||normal2<0.81f||normal2>1.21f) return false;
    const float t=Dot(delta,ray)/length2;
    const float endpointTolerance=0.01f/std::sqrt(length2);
    if (t<-endpointTolerance||t>1+endpointTolerance) return false;
    float residual[3]{};
    for (int i=0;i<3;++i) residual[i]=delta[i]-ray[i]*t;
    return Dot(residual,residual)<=0.0004f; // 2cm off-segment tolerance.
}
void WallPatch::Begin(float x,float y,float z) {
    origin_[0]=x; origin_[1]=y; origin_[2]=z; points_={};
}
void WallPatch::QueryRay(int index,float* from,float* to) const {
    const bool centre=index>=corners;
    if (centre) index-=corners;
    const float angle=(index%directions+(centre?0.5f:0))*6.28318530718f/directions;
    std::copy_n(origin_,3,from);
    from[2]+=bottom+(index/directions+(centre?0.5f:0))*0.5f;
    to[0]=from[0]+radius*std::cos(angle);
    to[1]=from[1]+radius*std::sin(angle);
    to[2]=from[2];
}
std::vector<skycraft::proto::ColTri> WallPatch::Triangles() const {
    using namespace skycraft::proto;
    std::vector<ColTri> result;
    result.reserve(directions*(levels-1)*2);
    auto valid=[&](int index) {
        float from[3]{},to[3]{}; QueryRay(index,from,to);
        const auto& p=points_[index];
        float toward[3]{};
        for (int i=0;i<3;++i) toward[i]=from[i]-p.position[i];
        return ValidSurfaceHit(from,to,p)&&std::abs(p.normal[2])<=0.5f&&
            Dot(p.normal,toward)>0.05f; // Reject grazing/back-face/start-inside hits.
    };
    auto add=[&](const SurfacePoint& a,const SurfacePoint& b,const SurfacePoint& c,const float* normal) {
        float ab[3]{},ac[3]{},cross[3]{};
        for (int i=0;i<3;++i) { ab[i]=b.position[i]-a.position[i]; ac[i]=c.position[i]-a.position[i]; }
        cross[0]=ab[1]*ac[2]-ab[2]*ac[1];
        cross[1]=ab[2]*ac[0]-ab[0]*ac[2];
        cross[2]=ab[0]*ac[1]-ab[1]*ac[0];
        if (Dot(cross,cross)<0.000001f) return;
        const SurfacePoint* vertices[]{&a,&b,&c};
        if (Dot(cross,normal)<0) std::swap(vertices[1],vertices[2]);
        ColTri tri{};
        for (int i=0;i<3;++i) {
            float converted[3]{};
            native::ToMinecraft(vertices[i]->position,converted);
            std::copy_n(converted,3,tri.v+i*3);
        }
        tri.flags=0; result.push_back(tri);
    };
    for (int z=0;z<levels-1;++z) for (int direction=0;direction<directions;++direction) {
        const int next=(direction+1)%directions;
        const int indices[]{z*directions+direction,z*directions+next,
            (z+1)*directions+direction,(z+1)*directions+next,corners+z*directions+direction};
        bool accepted=true;
        for (int index:indices) accepted&=valid(index);
        if (!accepted) continue;
        const auto& centre=points_[indices[4]];
        const float normalLength=std::sqrt(Dot(centre.normal,centre.normal));
        for (int i=0;i<4;++i) {
            const auto& p=points_[indices[i]];
            float delta[3]{};
            for (int j=0;j<3;++j) delta[j]=p.position[j]-centre.position[j];
            const float pLength=std::sqrt(Dot(p.normal,p.normal));
            if (Dot(p.normal,centre.normal)/(normalLength*pLength)<0.98f||
                std::abs(Dot(delta,centre.normal))/normalLength>0.08f) accepted=false;
        }
        const auto& a=points_[indices[0]]; const auto& b=points_[indices[1]];
        const auto& c=points_[indices[2]]; const auto& d=points_[indices[3]];
        if (!accepted||Distance(a,b)>2||Distance(c,d)>2||Distance(a,c)>0.75f||Distance(b,d)>0.75f) continue;
        add(a,b,c,centre.normal); add(b,d,c,centre.normal);
    }
    return result;
}
std::vector<SurfacePoint> WallPatch::NearbyFaces(const float* feet) const {
    std::vector<SurfacePoint> result;
    for(int i=0;i<samples;++i) {
        float from[3]{},to[3]{}; QueryRay(i,from,to);
        const auto& p=points_[i];
        const float delta[]{feet[0]-p.position[0],feet[1]-p.position[1],0};
        const float distance=std::hypot(delta[0],delta[1]);
        if(ValidSurfaceHit(from,to,p)&&std::abs(p.normal[2])<=0.05f&&Dot(delta,p.normal)>0.1f&&
            distance>=0.4f&&distance<=2.5f&&p.position[2]>=feet[2]+0.4f&&p.position[2]<=feet[2]+1.6f)
            result.push_back(p);
    }
    std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
        return std::hypot(a.position[0]-feet[0],a.position[1]-feet[1])<
            std::hypot(b.position[0]-feet[0],b.position[1]-feet[1]);
    });
    return result;
}
void BodyWallPatch::Begin(const WallPatch& coarse,const float* feet) {
    std::copy_n(feet,3,feet_); faces_=0; points_={};
    for(const auto& p:coarse.NearbyFaces(feet)) {
        const float length=std::hypot(p.normal[0],p.normal[1]);
        float n[]{p.normal[0]/length,p.normal[1]/length,0};
        bool duplicate=false;
        for(int i=0;i<faces_;++i) duplicate|=Dot(n,normal_[i])>0.98f;
        if(duplicate) continue;
        std::copy_n(n,3,normal_[faces_++]); if(faces_==2) break;
    }
}
void BodyWallPatch::QueryRay(int index,float* from,float* to) const {
    const int face=index/perFace; index%=perFace;
    const bool centre=index>=corners; if(centre) index-=corners;
    const int width=centre?columns-1:columns;
    const float side=-0.8f+(index%width+(centre?0.5f:0))*0.2f;
    const float height=(index/width+(centre?0.5f:0))*0.125f;
    from[0]=feet_[0]-normal_[face][1]*side;
    from[1]=feet_[1]+normal_[face][0]*side;
    from[2]=feet_[2]+height;
    to[0]=from[0]-normal_[face][0]*3;
    to[1]=from[1]-normal_[face][1]*3; to[2]=from[2];
}
std::vector<skycraft::proto::ColTri> BodyWallPatch::Triangles() const {
    using namespace skycraft::proto;
    std::vector<ColTri> result;
    for(int face=0;face<faces_;++face) for(int z=0;z<levels-1;++z) for(int x=0;x<columns-1;++x) {
        const int base=face*perFace;
        const int indices[]{base+z*columns+x,base+z*columns+x+1,
            base+(z+1)*columns+x,base+(z+1)*columns+x+1,base+corners+z*(columns-1)+x};
        bool accepted=true;
        const auto& centre=points_[indices[4]];
        for(int index:indices) {
            float from[3]{},to[3]{},delta[3]{},toward[3]{}; QueryRay(index,from,to);
            const auto& p=points_[index];
            for(int j=0;j<3;++j) { delta[j]=p.position[j]-centre.position[j]; toward[j]=from[j]-p.position[j]; }
            accepted&=ValidSurfaceHit(from,to,p)&&std::abs(p.normal[2])<=0.05f&&Dot(p.normal,toward)>0.05f&&
                Dot(p.normal,centre.normal)>0.98f*std::sqrt(Dot(p.normal,p.normal)*Dot(centre.normal,centre.normal))&&
                std::abs(Dot(delta,centre.normal))<=0.04f*std::sqrt(Dot(centre.normal,centre.normal));
        }
        if(!accepted) continue;
        for(auto corner:std::array<std::array<int,3>,2>{{{{0,1,2}},{{1,3,2}}}}) {
            const SurfacePoint* vertices[]{&points_[indices[corner[0]]],&points_[indices[corner[1]]],&points_[indices[corner[2]]]};
            float ab[3]{},ac[3]{},cross[3]{};
            for(int i=0;i<3;++i) {ab[i]=vertices[1]->position[i]-vertices[0]->position[i];ac[i]=vertices[2]->position[i]-vertices[0]->position[i];}
            cross[0]=ab[1]*ac[2]-ab[2]*ac[1];cross[1]=ab[2]*ac[0]-ab[0]*ac[2];cross[2]=ab[0]*ac[1]-ab[1]*ac[0];
            if(Dot(cross,cross)<1e-6f) continue;
            if(Dot(cross,centre.normal)<0) std::swap(vertices[1],vertices[2]);
            ColTri tri{};
            for(int i=0;i<3;++i) {
                float converted[3]{}; native::ToMinecraft(vertices[i]->position,converted);
                std::copy_n(converted,3,tri.v+i*3);
            }
            result.push_back(tri);
        }
    }
    return result;
}
}
