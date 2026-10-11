#pragma once
#include "DiagnosticSettle.h"
namespace witchcraft {
struct WallTarget { double point[3]{},normal[3]{}; };
// Select only a plane with sampled triangles across the MC body's approach.
bool ChooseWallTarget(const std::vector<skycraft::proto::ColTri>& mesh,const double* feet,WallTarget& out);
class DiagnosticWall {
public:
    enum class State { waiting, settling, approaching, braking, passed, failed };
    void Update(const GameFrame* game,const skycraft::proto::McState* mc,const DiagnosticWalls* walls,
        bool alive,bool focused,bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky);
    State Status() const { return state_; }
    const char* Reason() const { return reason_; }
    bool ForwardDown() const { return state_==State::approaching; }
    double Separation() const { return separation_; }
private:
    DiagnosticSettle settle_;
    State state_=State::waiting;
    WallTarget target_{};
    GameFrame anchor_{};
    double start_[3]{},contact_[3]{},separation_=0;
    float yaw_=0;
    std::uint32_t teleport_=0;
    std::uint64_t startedAt_=0,contactAt_=0,brakeAt_=0,freshAt_=0,frame_=0;
    bool contactSeen_=false;
    const char* reason_="waiting for a sampled wall 0.6 to 1.8 metres away";
};
}
