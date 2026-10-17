#pragma once
#include "skycraft_protocol.h"
#include <array>
#include <vector>

namespace witchcraft {
struct GroundPoint { float x=0, y=0, z=0, up=0; bool hit=false; };
// A bounded height patch. Centre samples reject holes/discontinuities while
// independently supported wedges preserve measured ground near missing corners.
class TerrainPatch {
public:
    static constexpr int cells = 16, corners = 17 * 17, samples = corners + 16 * 16;
    static constexpr float depth=24;
    void Begin(float x, float y, float z);
    void QueryPoint(int index, float& x, float& y) const;
    void Set(int index, const GroundPoint& point) { points_[index] = point; }
    std::vector<skycraft::proto::ColTri> Triangles() const;
    skycraft::proto::ColRegion Region(std::uint32_t epoch, std::uint32_t count) const;
    bool Contains(float x, float y,float margin=4) const;
    float Height() const { return z_; }
private:
    float x_=0, y_=0, z_=0;
    std::array<GroundPoint, samples> points_{};
};
// Finish and publish a patch before following ordinary movement. World changes
// and teleports invalidate it immediately, including a packet awaiting delivery.
class TerrainSchedule {
public:
    bool BeginIfNeeded(std::uint32_t world, std::uint32_t relocation,
        float x, float y, float z, std::uint64_t now,bool play=false);
    void Published(std::uint64_t now) { published_=true; publishedAt_=now; }
    TerrainPatch& Patch() { return patch_; }
private:
    TerrainPatch patch_;
    std::uint32_t world_=0, relocation_=0;
    std::uint64_t publishedAt_=0;
    bool started_=false, published_=false;
};
}
