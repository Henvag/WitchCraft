#include "ScriptBridge.h"
#include <cstdio>
using namespace witchcraft::script;
using namespace skycraft::proto;
int main() {
    Bridge bridge;bool published=false;
    if(bridge.Message(0,kOpHello,published)!=kHello||published) return 1;
    if(bridge.Message(0,kOpPlayActive,published)!=0) return 2;
    bridge.playActive=true;if(bridge.Message(0,kOpPlayActive,published)!=1) return 3;
    // One NPC at Witcher (10, 20, 3), heading 90, half health, hostile.
    bridge.Message(1,kOpActorsBegin,published);
    bridge.Message(7,kOpActorId,published);bridge.Message(10,kOpActorX,published);
    bridge.Message(20,kOpActorY,published);bridge.Message(3,kOpActorZ,published);
    bridge.Message(90,kOpActorHeading,published);bridge.Message(.5f,kOpActorHealth,published);
    bridge.Message(kActorHostile,kOpActorFlags,published);bridge.Message(5,kOpActorCommit,published);
    // An actor with a non-finite position is dropped.
    bridge.Message(8,kOpActorId,published);bridge.Message(NAN,kOpActorX,published);bridge.Message(0,kOpActorCommit,published);
    if(bridge.Message(0,kOpActorsEnd,published)!=1||!published) return 4;
    const auto& a=bridge.Actors()[0];
    if(a.formId!=7||a.x!=10||a.y!=3||a.z!=-20||a.yaw!=90||a.healthFrac!=.5f||a.flags!=kActorHostile) return 5;
    // Geralt loses a quarter of his vitality to actor 7's melee hit: 5 MC damage.
    bridge.Message(.25f,kOpHurtDamage,published);bridge.Message(7,kOpHurtAttacker,published);bridge.Message(kHurtMelee,kOpHurtKind,published);
    auto hurts=bridge.TakeHurts();
    if(hurts.size()!=1||hurts[0].kind!=kHurtMelee||hurts[0].a!=2500||hurts[0].b!=7) return 6; // 2500/100/5 = 5
    bridge.Message(0,kOpHurtKind,published);if(!bridge.TakeHurts().empty()) return 7; // No damage, no hurt.
    // Minecraft hits combine per actor and are handed out one at a time.
    bridge.QueueHit(7,3);bridge.QueueHit(7,2);bridge.QueueHit(9,4);bridge.QueueHit(0,5);bridge.QueueHit(9,NAN);
    if(bridge.Message(0,kOpNextHit,published)!=7||bridge.Message(0,kOpHitDamage,published)!=5) return 8;
    if(bridge.Message(0,kOpNextHit,published)!=9||bridge.Message(0,kOpHitDamage,published)!=4) return 9;
    if(bridge.Message(0,kOpNextHit,published)!=0||bridge.Message(0,kOpHitDamage,published)!=0) return 10;
    if(bridge.Message(0,999,published)!=0) return 11;
    std::puts("script bridge checks passed");
}
