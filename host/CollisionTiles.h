#pragma once
#include "skycraft_protocol.h"
#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

namespace witchcraft {
// SkyCollision.readTris stores a packet under floor(minCorner/8) only.
// Duplicate a triangle into every cube touched by its bounds so MC's local
// lookup finds it. Keep accepted keys when a new patch cancels pending work.
class CollisionTiles {
public:
    static constexpr int size=8;
    using Key=std::array<int,3>;
    struct Packet {
        Key key{};
        std::uint32_t type=skycraft::proto::kColTris;
        std::uint32_t count=0;
        bool retain=false;
        std::vector<std::uint8_t> bytes;
    };
    // `dug(x, y, z)` removes voxel support from Minecraft cells dug out of the land.
    bool Prepare(const skycraft::proto::ColRegion& bounds,
        const std::vector<skycraft::proto::ColTri>& triangles,
        const std::function<bool(int,int,int)>& dug={});
    void Reset() { resident_.clear(); pending_.clear(); next_=0; }
    bool Pending() const { return next_<pending_.size(); }
    const Packet& Front() const { return pending_[next_]; }
    void Accepted();
    std::size_t PacketCount() const { return pending_.size(); }
private:
    std::set<Key> resident_;
    std::vector<Packet> pending_;
    std::size_t next_=0;
};
}
