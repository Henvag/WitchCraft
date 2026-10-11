#include "DiagnosticWall.h"
#include <cmath>
#include <algorithm>
namespace witchcraft {
namespace {
double Dot(const double* a,const double* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
void Cross(const double* a,const double* b,double* c) {
    c[0]=a[1]*b[2]-a[2]*b[1]; c[1]=a[2]*b[0]-a[0]*b[2]; c[2]=a[0]*b[1]-a[1]*b[0];
}
bool Contains(const skycraft::proto::ColTri& tri,const double* p) {
    double a[3],b[3],v[3],n[3];
    for(int i=0;i<3;++i) { a[i]=tri.v[3+i]-tri.v[i]; b[i]=tri.v[6+i]-tri.v[i]; v[i]=p[i]-tri.v[i]; }
    Cross(a,b,n); const double length=std::sqrt(Dot(n,n));
    if(!std::isfinite(length)||length<1e-6||std::abs(Dot(v,n))/length>0.04) return false;
    const double aa=Dot(a,a),bb=Dot(b,b),ab=Dot(a,b),av=Dot(a,v),bv=Dot(b,v),den=aa*bb-ab*ab;
    const double u=(bb*av-ab*bv)/den,w=(aa*bv-ab*av)/den;
    return u>=-0.001&&w>=-0.001&&u+w<=1.001;
}
}
bool ChooseWallTarget(const std::vector<skycraft::proto::ColTri>& mesh,const double* feet,WallTarget& out) {
    double best=1.8; bool found=false;
    for(const auto& tri:mesh) {
        double a[3],b[3],n[3],v[3];
        for(int i=0;i<3;++i) { a[i]=tri.v[3+i]-tri.v[i]; b[i]=tri.v[6+i]-tri.v[i]; v[i]=feet[i]-tri.v[i]; }
        Cross(a,b,n); const double length=std::sqrt(Dot(n,n));
        if(!std::isfinite(length)||length<1e-6) continue;
        for(double& x:n) x/=length;
        if(std::abs(n[1])>0.05) continue; // This test accepts nearly vertical walls only.
        if(Dot(v,n)<0) for(double& x:n) x=-x;
        const double horizontal=std::hypot(n[0],n[2]);
        n[0]/=horizontal; n[1]=0; n[2]/=horizontal;
        const double distance=Dot(v,n);
        if(!std::isfinite(distance)||distance<0.6||distance>best) continue;
        WallTarget candidate;
        for(int i=0;i<3;++i) { candidate.normal[i]=n[i]; candidate.point[i]=feet[i]-distance*n[i]; }
        bool covered=true;
        for(double h:{0.2,0.8,1.4}) for(double side:{-0.35,0.0,0.35}) {
            double point[]{candidate.point[0]-n[2]*side,feet[1]+h,candidate.point[2]+n[0]*side};
            bool hit=false;
            for(const auto& t:mesh) if(Contains(t,point)) { hit=true; break; }
            covered&=hit;
        }
        if(covered) { out=candidate; best=distance; found=true; }
    }
    return found;
}
void DiagnosticWall::Update(const GameFrame* game,const skycraft::proto::McState* mc,const DiagnosticWalls* walls,
    bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky) {
    using namespace skycraft::proto;
    auto fail=[&](const char* why){state_=State::failed;reason_=why;};
    if(state_==State::waiting) {
        sky.flags=kSkyMenuOpen;
        if(!game||!walls||!alive||!focused||!consumed||!(game->flags&kGameDiagnosticPatch)||
            (game->flags&(kGameMenu|kGameLoading))||walls->world!=game->worldId||walls->epoch!=game->collisionEpoch||
            walls->relocation!=game->relocation||!ChooseWallTarget(walls->triangles,game->feet,target_)) return;
        state_=State::settling;
    }
    if(state_==State::settling) {
        settle_.Update(game,mc,alive,focused,consumed,now,sky); reason_=settle_.Reason();
        if(settle_.Status()==DiagnosticSettle::State::failed) { fail(settle_.Reason()); return; }
        if(settle_.Status()!=DiagnosticSettle::State::passed) return;
        if(!walls||!ChooseWallTarget(walls->triangles,game->feet,target_)) { fail("sampled wall unavailable after settling"); return; }
        anchor_=*game; teleport_=sky.teleportSeq;
        start_[0]=mc->curX; start_[1]=mc->curY; start_[2]=mc->curZ;
        yaw_=static_cast<float>(std::atan2(target_.normal[0],-target_.normal[2])*57.295779513);
        startedAt_=freshAt_=now; frame_=mc->frameCounter;
        state_=State::approaching; reason_="walking into the sampled wall";
        const double offset=std::hypot(start_[0]-anchor_.feet[0],start_[2]-anchor_.feet[2]);
        const double height=start_[1]-anchor_.feet[1];
        if(!std::isfinite(offset)||!std::isfinite(height)||offset>0.5||std::abs(height)>0.6)
            fail("raw MC tick position did not confirm arrival");
    }
    if(state_==State::approaching||state_==State::braking) {
        const bool valid=game&&mc&&alive&&focused&&(game->flags&kGamePlaying)&&(game->flags&kGameDiagnosticPatch)&&
            !(game->flags&(kGameMenu|kGameLoading))&&(mc->flags&kMcInWorld)&&(mc->flags&kMcOnGround)&&
            !(mc->flags&(kMcDead|kMcFlying|kMcScreenOpen))&&game->worldId==anchor_.worldId&&
            game->relocation==anchor_.relocation&&game->collisionEpoch==anchor_.collisionEpoch&&mc->teleportAck==teleport_;
        if(!valid) fail("ground contact, focus, world or link changed");
        else {
            double moved=0; for(int i=0;i<3;++i) moved+=std::pow(game->feet[i]-anchor_.feet[i],2);
            const double delta[]{mc->curX-target_.point[0],mc->curY-target_.point[1],mc->curZ-target_.point[2]};
            separation_=Dot(delta,target_.normal);
            const double dx=mc->curX-start_[0],dz=mc->curZ-start_[2];
            const double lateral=std::abs(dx*target_.normal[2]-dz*target_.normal[0]);
            if(!std::isfinite(moved)||moved>0.0625) fail("Geralt moved during wall test");
            else if(!std::isfinite(separation_)||!std::isfinite(lateral)||!std::isfinite(mc->curY)||
                separation_<0.15||std::hypot(dx,dz)>2.5||lateral>0.3||std::abs(mc->curY-start_[1])>0.6)
                fail("MC crossed wall plane or left bounded approach");
            else {
                if(frame_!=mc->frameCounter) { frame_=mc->frameCounter; freshAt_=now; }
                if(now-freshAt_>1000) fail("MC reports stopped updating");
                else if(state_==State::approaching) {
                    if(separation_<=0.45) {
                        if(!contactSeen_) { contactSeen_=true; contactAt_=now; contact_[0]=mc->curX; contact_[2]=mc->curZ; }
                        if(std::hypot(mc->curX-contact_[0],mc->curZ-contact_[2])>0.08) {
                            contactAt_=now; contact_[0]=mc->curX; contact_[2]=mc->curZ;
                        }
                        if(now-contactAt_>=500&&-dx*target_.normal[0]-dz*target_.normal[2]>=0.15) {
                            state_=State::braking; brakeAt_=now; reason_="wall blocked forward input; key released";
                        }
                    } else contactSeen_=false;
                    if(state_==State::approaching&&now-startedAt_>2000) fail("no confirmed wall contact within two seconds");
                } else if(now-brakeAt_>=500) {
                    if(separation_>0.45||std::hypot(mc->curX-contact_[0],mc->curZ-contact_[2])>0.08)
                        fail("MC moved away after wall-contact release");
                    else { state_=State::passed; reason_="sampled wall blocked MC; release and stop confirmed"; }
                }
            }
        }
    }
    if(state_==State::failed&&teleport_==0) {
        settle_.Update(game,mc,alive,focused,consumed,now,sky); sky.flags=kSkyMenuOpen; return;
    }
    sky.teleportSeq=teleport_; sky.posX=anchor_.feet[0]; sky.posY=anchor_.feet[1]+0.5; sky.posZ=anchor_.feet[2]; sky.yaw=yaw_;
    const bool active=state_==State::approaching||state_==State::braking;
    if(active) { sky.worldId=anchor_.worldId; sky.collisionEpoch=anchor_.collisionEpoch; }
    sky.flags=active?kSkyInGame:kSkyMenuOpen;
}
}
