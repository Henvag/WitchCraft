#pragma once
#include "DiagnosticSettle.h"
namespace witchcraft {
class DiagnosticAvatar {
public:
    void Update(const GameFrame* game,const skycraft::proto::McState* mc,bool alive,bool focused,
        bool consumed,std::uint64_t now,skycraft::proto::SkyState& sky);
    bool Visible() const { return visible_; }
    const char* Reason() const { return reason_; }
private:
    DiagnosticSettle settle_;
    GameFrame anchor_{};
    bool anchored_=false,visible_=false;
    std::uint32_t sequence_=0;
    const char* reason_="waiting for settled Minecraft avatar";
};
}
