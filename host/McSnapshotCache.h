#pragma once
#include "skycraft_protocol.h"
#include <cstdint>
namespace witchcraft {
// A seqlock read may lose a race with the writer even with a healthy peer.
// Retain only a previously coherent snapshot, bound to that peer and 100 ms.
class McSnapshotCache {
    skycraft::proto::McState state_{};
    std::uint32_t peer_=0;
    std::uint64_t at_=0;
    bool valid_=false;
public:
    bool Update(bool alive,std::uint32_t peer,bool received,
        const skycraft::proto::McState& candidate,std::uint64_t now,
        skycraft::proto::McState& out) {
        out={};
        if(!alive||!peer) {valid_=false;peer_=0;return false;}
        if(peer!=peer_) {valid_=false;peer_=peer;}
        if(received) {state_=candidate;at_=now;valid_=true;}
        if(!valid_||now<at_||now-at_>100) {valid_=false;return false;}
        out=state_;return true;
    }
};
}
