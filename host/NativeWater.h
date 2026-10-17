#pragma once
#include "skycraft_protocol.h"
#include <cmath>
#include <cstdint>
#include <cstring>
namespace witchcraft::native {
// CWorld::GetWaterLevel (script thunk RVA 0x22a3f80 -> 0x22afdb0) reads the
// CGlobalWater at world + 0x57c0 and, for the scripts' default approximate
// query, calls RVA 0x23fd3d0(water, x, y). It returns -10000 where the world
// has no water. All values are build-specific; validate before calling.
inline constexpr std::uintptr_t gameWorldVtable=0x389c3c8,globalWaterVtable=0x399a990;
inline constexpr std::uintptr_t worldWaterOffset=0x57c0,waterLevelApprox=0x23fd3d0;
inline constexpr std::uint8_t waterLevelPrologue[]{0x40,0x56,0x48,0x83,0xec,0x50,0xc5,0xf8,0x29,0x7c,0x24,0x30,
    0xc5,0x78,0x29,0x44,0x24,0x20,0x48,0x89,0x5c,0x24,0x60,0x48,0x8b,0xf1,0x48,0x8b,0x59,0x68};
inline constexpr float kNativeNoWater=-10000.f;

// One native reading as a Minecraft surface height (Witcher Z is Minecraft Y).
inline float WaterSurface(float level) {
    return std::isfinite(level)&&level>kNativeNoWater+1&&std::abs(level)<100000?level:skycraft::proto::kNoWater;
}
// Columns around a Minecraft position; `level(x, y)` takes Witcher X/Y.
template<class Level>
skycraft::proto::WaterGrid BuildWaterGrid(double mcX,double mcZ,std::uint32_t worldId,Level&& level) {
    using namespace skycraft::proto;
    constexpr int size=static_cast<int>(kWaterGridSize);
    WaterGrid grid{};
    grid.originX=static_cast<std::int32_t>(std::floor(mcX))-size/2;
    grid.originZ=static_cast<std::int32_t>(std::floor(mcZ))-size/2;
    grid.worldId=worldId;
    for(int z=0;z<size;++z) for(int x=0;x<size;++x) {
        const float wx=static_cast<float>(grid.originX+x+.5),wy=static_cast<float>(-(grid.originZ+z+.5));
        grid.surface[z*size+x]=WaterSurface(level(wx,wy));
    }
    return grid;
}
inline skycraft::proto::WaterGrid EmptyWaterGrid(std::uint32_t worldId) {
    skycraft::proto::WaterGrid grid{};grid.worldId=worldId;
    for(auto& value:grid.surface) value=skycraft::proto::kNoWater;
    return grid;
}
}
