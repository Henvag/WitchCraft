#include "DiagnosticWall.h"
#include "Walls.h"
#include <cstdio>
#include <cstdlib>
using namespace witchcraft;
using namespace skycraft::proto;
void Check(bool v) { if(!v) { puts("wall diagnostic check failed"); std::exit(1); } }
std::vector<ColTri> Mesh() {
    ColTri a{},b{};
    const float av[]{-2,-1,1,2,-1,1,-2,3,1},bv[]{2,-1,1,2,3,1,-2,3,1};
    for(int i=0;i<9;++i){a.v[i]=av[i];b.v[i]=bv[i];}
    return {a,b};
}
struct Fixture {
    DiagnosticWall test; GameFrame game{}; McState mc{}; SkyState sky{}; DiagnosticWalls walls;
    Fixture() { game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
        game.worldId=game.collisionEpoch=game.relocation=1;
        mc.flags=kMcInWorld|kMcOnGround; walls={1,1,1,Mesh()}; }
    void Update(std::uint64_t now,bool focus=true,bool alive=true) {
        ++mc.frameCounter; test.Update(&game,&mc,&walls,alive,focus,true,now,sky);
    }
    void Start() {
        Update(0); Update(2000); Check(!test.ForwardDown());
        mc.teleportAck=sky.teleportSeq; Update(2100); Update(2600);
        Check(test.Status()==DiagnosticWall::State::approaching&&test.ForwardDown());
    }
};
int main() {
    WallTarget target; double feet[]{0,0,0};
    Check(ChooseWallTarget(Mesh(),feet,target)&&target.normal[2]<-0.99);
    Check(!ChooseWallTarget({},feet,target));
    feet[2]=0.7; Check(!ChooseWallTarget(Mesh(),feet,target));
    feet[2]=-1; Check(!ChooseWallTarget(Mesh(),feet,target));
    feet[2]=0; auto partial=Mesh(); partial.resize(1);
    Check(!ChooseWallTarget(partial,feet,target));
    // A thin projecting base rejects the coarse half-metre cell, although the
    // wall at MC ankle height is continuous. Fine rays must recover only the
    // actually sampled surface above the base, preserving any missing cell.
    auto trace=[](const float* from,const float* to) {
        SurfacePoint p{};
        const float plane=from[2]<0.125f?0.8f:1.0f;
        const float dx=to[0]-from[0];
        if(dx<=0) return p;
        const float t=(plane-from[0])/dx;
        if(t<=0||t>1) return p;
        for(int i=0;i<3;++i) p.position[i]=from[i]+t*(to[i]-from[i]);
        p.normal[0]=-1; p.hit=true; return p;
    };
    WallPatch coarse; coarse.Begin(0,0,0);
    for(int i=0;i<WallPatch::samples;++i){float a[3]{},b[3]{};coarse.QueryRay(i,a,b);coarse.Set(i,trace(a,b));}
    Check(!ChooseWallTarget(coarse.Triangles(),feet,target));
    BodyWallPatch detail; const float nativeFeet[]{0,0,0}; detail.Begin(coarse,nativeFeet);
    Check(detail.Samples()==BodyWallPatch::perFace);
    for(int i=0;i<detail.Samples();++i){float a[3]{},b[3]{};detail.QueryRay(i,a,b);detail.Set(i,trace(a,b));}
    const auto fine=detail.Triangles();
    Check(ChooseWallTarget(fine,feet,target)&&target.normal[0]<-0.99);
    const auto complete=fine.size(); detail.Set(BodyWallPatch::corners+8*(BodyWallPatch::columns-1)+4,{});
    Check(detail.Triangles().size()==complete-2);
    WallPatch empty; empty.Begin(0,0,0); detail.Begin(empty,nativeFeet); Check(detail.Samples()==0&&detail.Triangles().empty());
    Fixture pass; pass.Start(); pass.mc.curZ=0.7; pass.Update(2800); pass.Update(3300);
    Check(pass.test.Status()==DiagnosticWall::State::braking&&!pass.test.ForwardDown());
    pass.Update(3800); Check(pass.test.Status()==DiagnosticWall::State::passed&&pass.sky.flags==kSkyMenuOpen);
    Fixture cross; cross.Start(); cross.mc.curZ=0.9; cross.Update(2800);
    Check(cross.test.Status()==DiagnosticWall::State::failed&&!cross.test.ForwardDown());
    Fixture noWall; noWall.Start(); noWall.Update(4601);
    Check(noWall.test.Status()==DiagnosticWall::State::failed&&!noWall.test.ForwardDown());
    Fixture focus; focus.Start(); focus.Update(2700,false);
    Check(focus.test.Status()==DiagnosticWall::State::failed&&!focus.test.ForwardDown());
    Fixture dead; dead.Start(); dead.Update(2700,true,false);
    Check(dead.test.Status()==DiagnosticWall::State::failed&&!dead.test.ForwardDown());
    Fixture frozen; frozen.Start(); frozen.test.Update(&frozen.game,&frozen.mc,&frozen.walls,true,true,true,3601,frozen.sky);
    Check(frozen.test.Status()==DiagnosticWall::State::failed&&!frozen.test.ForwardDown());
    Fixture ground; ground.Start(); ground.mc.flags&=~kMcOnGround; ground.Update(2700);
    Check(ground.test.Status()==DiagnosticWall::State::failed&&!ground.test.ForwardDown());
    Fixture world; world.Start(); ++world.game.worldId; world.Update(2700);
    Check(world.test.Status()==DiagnosticWall::State::failed&&!world.test.ForwardDown());
    Fixture moving; moving.Start(); moving.game.feet[0]=0.3; moving.Update(2700);
    Check(moving.test.Status()==DiagnosticWall::State::failed&&!moving.test.ForwardDown());
    Fixture drift; drift.Start(); drift.mc.curZ=0.7; drift.Update(2800); drift.Update(3300);
    drift.mc.curZ=0.5; drift.Update(3800); Check(drift.test.Status()==DiagnosticWall::State::failed);
    Fixture wait; wait.walls.triangles.clear(); wait.Update(0); wait.Update(4000);
    Check(wait.test.Status()==DiagnosticWall::State::waiting&&!wait.test.ForwardDown());
    puts("wall selection, body coverage, contact, crossing, timeout, release and handoff checks passed");
}
