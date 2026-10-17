#pragma once
#include "skycraft_protocol.h"
#include <array>
#include <cmath>
#include <vector>

namespace witchcraft {
// Coarse scene depth from the Witcher camera: rays through a screen grid hit
// whatever has native collision (buildings, rocks, trees), and the hits become
// depth-only triangles so Minecraft geometry behind them is hidden. The sampled
// terrain/wall patch still provides exact occlusion close to the player.
class ViewDepthGrid {
public:
    static constexpr int columns=32,rows=18,count=(columns+1)*(rows+1);
    static constexpr float range=96;
    // Witcher coordinates (X/Y horizontal, Z up). `verticalFov` in degrees.
    bool Begin(const float* camera,const float* forward,float verticalFov,float aspect) {
        for(int i=0;i<3;++i) if(!std::isfinite(camera[i])||!std::isfinite(forward[i])) return false;
        const float length=std::hypot(forward[0],forward[1],forward[2]);
        if(length<.5f||!std::isfinite(verticalFov)||verticalFov<20||verticalFov>140||!(aspect>.5f&&aspect<4)) return false;
        for(int i=0;i<3;++i) {origin_[i]=camera[i];forward_[i]=forward[i]/length;}
        // Right = forward x up, up' = right x forward (Z up).
        float right[3]{forward_[1],-forward_[0],0};
        float rl=std::hypot(right[0],right[1]);
        if(rl<1e-4f) {right[0]=1;right[1]=0;rl=1;}
        for(auto& v:right) v/=rl;
        const float up[3]{right[1]*forward_[2]-right[2]*forward_[1],right[2]*forward_[0]-right[0]*forward_[2],
            right[0]*forward_[1]-right[1]*forward_[0]};
        const float ty=std::tan(verticalFov*.5f*0.01745329252f)*1.05f,tx=ty*aspect;
        for(int i=0;i<3;++i) {right_[i]=right[i]*tx;up_[i]=up[i]*ty;}
        hits_.fill({});
        return true;
    }
    void QueryRay(int index,float* from,float* to) const {
        const float u=(index%(columns+1))/float(columns)*2-1,v=1-(index/(columns+1))/float(rows)*2;
        float d[3]{};for(int i=0;i<3;++i) d[i]=forward_[i]+right_[i]*u+up_[i]*v;
        const float l=std::hypot(d[0],d[1],d[2]);
        for(int i=0;i<3;++i) {from[i]=origin_[i];to[i]=origin_[i]+d[i]/l*range;}
    }
    void Set(int index,bool hit,const float* position) {
        auto& h=hits_[index];h.hit=hit;
        if(hit) for(int i=0;i<3;++i) h.position[i]=position[i];
    }
    // Depth-only triangles in Minecraft coordinates. Hits are pushed `bias`
    // metres further along their rays so Minecraft blocks resting on a native
    // surface are never cut by the coarse grid crossing that surface.
    std::vector<skycraft::proto::ColTri> Triangles(float bias=.35f) const {
        std::vector<skycraft::proto::ColTri> out;
        auto depth=[&](const Hit& h) {return std::hypot(h.position[0]-origin_[0],h.position[1]-origin_[1],h.position[2]-origin_[2]);};
        auto point=[&](const Hit& h,float* mc) {
            float d[3]{};for(int i=0;i<3;++i) d[i]=h.position[i]-origin_[i];
            const float l=std::fmax(1e-3f,std::hypot(d[0],d[1],d[2]));
            float w[3]{};for(int i=0;i<3;++i) w[i]=h.position[i]+d[i]/l*bias;
            mc[0]=w[0];mc[1]=w[2];mc[2]=-w[1];
        };
        for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
            const Hit* q[]{&hits_[y*(columns+1)+x],&hits_[y*(columns+1)+x+1],
                &hits_[(y+1)*(columns+1)+x],&hits_[(y+1)*(columns+1)+x+1]};
            if(!q[0]->hit||!q[1]->hit||!q[2]->hit||!q[3]->hit) continue;
            float lo=depth(*q[0]),hi=lo;
            for(const auto* h:q) {const float d=depth(*h);lo=std::fmin(lo,d);hi=std::fmax(hi,d);}
            // A depth step between corners is an object edge, not a surface.
            if(hi>lo*1.15f+.5f) continue;
            float p[4][3]{};for(int i=0;i<4;++i) point(*q[i],p[i]);
            skycraft::proto::ColTri a{},b{};
            for(int i=0;i<3;++i) {a.v[i]=p[0][i];a.v[3+i]=p[1][i];a.v[6+i]=p[2][i];
                b.v[i]=p[1][i];b.v[3+i]=p[3][i];b.v[6+i]=p[2][i];}
            out.push_back(a);out.push_back(b);
        }
        return out;
    }
private:
    struct Hit {float position[3]{};bool hit=false;};
    float origin_[3]{},forward_[3]{0,1,0},right_[3]{},up_[3]{};
    std::array<Hit,count> hits_{};
};
}
