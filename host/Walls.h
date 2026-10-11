#pragma once
#include "skycraft_protocol.h"
#include <array>
#include <vector>

namespace witchcraft {
struct SurfacePoint {
    float position[3]{}, normal[3]{};
    bool hit=false;
};
// Validate a native hit against a bounded segment before any mesh uses it.
bool ValidSurfaceHit(const float* from, const float* to, const SurfacePoint& hit);

// Diagnostic nearest static surfaces around a fixed origin. Five samples must
// agree on a local plane; gaps and changes of surface leave cells empty.
class WallPatch {
public:
    static constexpr int directions=32, levels=9;
    static constexpr int corners=directions*levels;
    static constexpr int samples=corners+directions*(levels-1);
    static constexpr float radius=8, bottom=-1, top=3;
    void Begin(float x,float y,float z);
    void QueryRay(int index,float* from,float* to) const;
    void Set(int index,const SurfacePoint& point) { points_[index]=point; }
    std::vector<skycraft::proto::ColTri> Triangles() const;
    std::vector<SurfacePoint> NearbyFaces(const float* feet) const;
private:
    float origin_[3]{};
    std::array<SurfacePoint,samples> points_{};
};
// Parallel local rays refine up to two nearby wall orientations around the body.
class BodyWallPatch {
public:
    static constexpr int columns=9,levels=17,corners=columns*levels;
    static constexpr int perFace=corners+(columns-1)*(levels-1);
    void Begin(const WallPatch& coarse,const float* feet);
    int Samples() const { return faces_*perFace; }
    void QueryRay(int index,float* from,float* to) const;
    void Set(int index,const SurfacePoint& point) { points_[index]=point; }
    std::vector<skycraft::proto::ColTri> Triangles() const;
private:
    int faces_=0;
    float feet_[3]{},normal_[2][3]{};
    std::array<SurfacePoint,perFace*2> points_{};
};
}
