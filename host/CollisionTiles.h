#pragma once
#include "skycraft_protocol.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
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
    // Tiles the latest prepared patch covers with its own sampled collision.
    const std::set<Key>& Covered() const { return covered_; }
private:
    std::set<Key> covered_;
    std::set<Key> resident_;
    std::vector<Packet> pending_;
    std::size_t next_=0;
};
// Short-lived collision ahead of fast movement (Elytra, sprint flight), in tiles
// the sampled patch does not cover yet. Each probe tile expires on its own and
// is then emptied, unless the patch has taken that tile over meanwhile.
class ProbeTiles {
public:
    using Key=CollisionTiles::Key;
    using Packet=CollisionTiles::Packet;
    static constexpr std::size_t maxPerTile=512;
    static constexpr std::uint64_t lifetimeMs=1500;
    void Add(const std::vector<skycraft::proto::ColTri>& triangles,std::uint32_t epoch,
        std::uint64_t now,const std::set<Key>& covered);
    void Expire(std::uint64_t now,std::uint32_t epoch,const std::set<Key>& covered);
    void Reset() {live_.clear();pending_.clear();}
    bool Pending() const {return !pending_.empty();}
    const Packet& Front() const {return pending_.front();}
    void Accepted() {if(!pending_.empty()) pending_.erase(pending_.begin());}
    std::size_t LiveTiles() const {return live_.size();}
private:
    struct Tile {std::vector<skycraft::proto::ColTri> triangles;std::uint64_t expiry=0;};
    void Queue(const Key& key,const std::vector<skycraft::proto::ColTri>& triangles,std::uint32_t epoch);
    std::map<Key,Tile> live_;
    std::vector<Packet> pending_;
};
}
