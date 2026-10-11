#pragma once
#include "NativeLayout.h"
#include "Terrain.h"
#include "Walls.h"
namespace witchcraft::native {
enum class TraceStatus { miss, hit, busy, unavailable, invalid };
bool ValidateTraceCode(const Reader& reader, std::uintptr_t base);
TraceStatus TraceSurface(const Reader& reader, std::uintptr_t base, const Sample& sample,
    const float* from, const float* to, SurfacePoint& result);
TraceStatus TraceGround(const Reader& reader, std::uintptr_t base, const Sample& sample,
    float x, float y, float height, GroundPoint& result);
}
