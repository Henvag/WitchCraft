#pragma once
#include "skycraft_protocol.h"
#include <vector>
namespace witchcraft {
// Bounded sub-voxel solid support beneath sampled height-field triangles.
// Exact triangles remain the local player's movement representation.
std::vector<skycraft::proto::ColBlock> TerrainVoxels(
    const skycraft::proto::ColRegion&,const std::vector<skycraft::proto::ColTri>&);
}
