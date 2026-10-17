#pragma once
#include "skycraft_protocol.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

// Channel to the companion script mod (mods/modWitchCraft). Witcher health,
// damage and attitudes live in WitcherScript, so the mod reports nearby actors
// and Geralt's damage, and applies Minecraft's hits. It talks through the native
// ClampF(v, min, max): a call whose `min` is kScriptMagic is a message, `max`
// is the operation and `v` its value; every other call clamps as before.
namespace witchcraft::script {
inline constexpr float kScriptMagic=-7777777.f;
inline constexpr float kHello=4242.f; // Returned for kOpHello when the DLL is present.
enum Op : int {
    kOpHello=1,kOpPlayActive=2,
    kOpActorsBegin=10,kOpActorId=11,kOpActorX=12,kOpActorY=13,kOpActorZ=14,kOpActorHeading=15,
    kOpActorHealth=16,kOpActorFlags=17,kOpActorLevel=18,kOpActorCommit=19,kOpActorsEnd=20,
    kOpHurtDamage=30,kOpHurtAttacker=31,kOpHurtKind=32,
    kOpNextHit=40,kOpHitDamage=41,
};
struct Hurt {std::uint16_t kind=skycraft::proto::kHurtOther;std::int32_t a=0,b=0;};
struct Hit {std::uint32_t actor=0;float damage=0;};

// Pure message state; the game thread drives it, Link I/O stays with callers.
class Bridge {
public:
    bool playActive=false;
    // Returns the reply. `published` is set once a complete actor list arrives.
    float Message(float value,int op,bool& published) {
        using namespace skycraft::proto;
        published=false;
        const bool finite=std::isfinite(value);
        switch(op) {
        case kOpHello: return kHello;
        case kOpPlayActive: return playActive?1.f:0.f;
        case kOpActorsBegin: building_.clear();current_={};return 1;
        case kOpActorId:
            current_={};current_.formId=finite&&value>=1&&value<16777216?static_cast<std::uint32_t>(value):0;
            current_.width=.7f;current_.height=1.9f;current_.healthFrac=1;return 1;
        case kOpActorX: wx_=finite?value:NAN;return 1;
        case kOpActorY: wy_=finite?value:NAN;return 1;
        case kOpActorZ: wz_=finite?value:NAN;return 1;
        case kOpActorHeading: current_.yaw=finite?180.f-value:0;return 1;
        case kOpActorHealth: current_.healthFrac=finite?std::fmin(1.f,std::fmax(0.f,value)):0;return 1;
        case kOpActorFlags: current_.flags=finite&&value>=0&&value<16?static_cast<std::uint32_t>(value):0;return 1;
        case kOpActorLevel: current_.level=finite&&value>=0&&value<1000?static_cast<std::uint16_t>(value):0;return 1;
        case kOpActorCommit:
            // Witcher X/Y horizontal, Z up -> Minecraft X, Y up, Z = -Witcher Y.
            if(current_.formId&&std::isfinite(wx_)&&std::isfinite(wy_)&&std::isfinite(wz_)&&
                std::abs(wx_)<1000000&&std::abs(wy_)<1000000&&std::abs(wz_)<1000000&&building_.size()<kMaxActors) {
                current_.x=wx_;current_.y=wz_;current_.z=-wy_;building_.push_back(current_);
            }
            current_={};wx_=wy_=wz_=NAN;return 1;
        case kOpActorsEnd: actors_=building_;published=true;return static_cast<float>(actors_.size());
        case kOpHurtDamage: hurtFraction_=finite?std::fmin(1.f,std::fmax(0.f,value)):0;return 1;
        case kOpHurtAttacker: hurtAttacker_=finite&&value>=0&&value<16777216?static_cast<std::int32_t>(value):0;return 1;
        case kOpHurtKind:
            if(hurtFraction_>0&&hurts_.size()<64) {
                // Geralt's lost vitality fraction as 20 Minecraft health; kInHurt
                // carries "Skyrim damage x 100", which SkyCombat divides by 5.
                Hurt hurt;hurt.kind=finite&&value>=0&&value<=3?static_cast<std::uint16_t>(value):kHurtOther;
                hurt.a=static_cast<std::int32_t>(std::lround(hurtFraction_*20*5*100));hurt.b=hurtAttacker_;
                hurts_.push_back(hurt);
            }
            hurtFraction_=0;hurtAttacker_=0;return 1;
        case kOpNextHit:
            if(hits_.empty()) {lastHit_={};return 0;}
            lastHit_=hits_.front();hits_.pop_front();return static_cast<float>(lastHit_.actor);
        case kOpHitDamage: return lastHit_.damage;
        default: return 0;
        }
    }
    void QueueHit(std::uint32_t actor,float damage) {
        if(!actor||!std::isfinite(damage)||damage<=0) return;
        for(auto& hit:hits_) if(hit.actor==actor) {hit.damage+=damage;return;}
        if(hits_.size()<64) hits_.push_back({actor,damage});
    }
    const std::vector<skycraft::proto::ActorRecord>& Actors() const {return actors_;}
    std::vector<Hurt> TakeHurts() {auto out=std::move(hurts_);hurts_.clear();return out;}
private:
    std::vector<skycraft::proto::ActorRecord> building_,actors_;
    skycraft::proto::ActorRecord current_{};
    float wx_=NAN,wy_=NAN,wz_=NAN,hurtFraction_=0;
    std::int32_t hurtAttacker_=0;
    std::vector<Hurt> hurts_;
    std::deque<Hit> hits_;
    Hit lastHit_{};
};
}
