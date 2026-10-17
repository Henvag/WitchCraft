#include "NativeWater.h"
#include <cstdio>
using namespace witchcraft::native;
using namespace skycraft::proto;
int main() {
    if(WaterSurface(kNativeNoWater)!=kNoWater||WaterSurface(std::nanf(""))!=kNoWater||WaterSurface(1e9f)!=kNoWater) return 1;
    if(WaterSurface(-3.25f)!=-3.25f||WaterSurface(0)!=0) return 2;
    // A lake whose surface is at Witcher Z 12 for Witcher X >= 100.
    unsigned calls=0;
    const auto grid=BuildWaterGrid(100.4,-50.7,7,[&](float x,float) {
        ++calls;
        return x>=100?12.f:kNativeNoWater;
    });
    if(calls!=kWaterGridSize*kWaterGridSize||grid.worldId!=7) return 3;
    if(grid.originX!=92||grid.originZ!=-59) return 4; // floor(centre) - 8
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        const float expected=grid.originX+x+.5f>=100?12.f:kNoWater;
        if(grid.surface[z*16+x]!=expected) return 5;
    }
    // Minecraft Z maps to negative Witcher Y for every sampled column.
    float lastY=0;
    BuildWaterGrid(0,10,1,[&](float,float y) {lastY=y;return 0.f;});
    if(std::abs(lastY-(-(10-8+15+.5f)))>1e-4f) return 6;
    const auto empty=EmptyWaterGrid(9);
    if(empty.worldId!=9) return 7;
    for(const float value:empty.surface) if(value!=kNoWater) return 8;
    std::puts("water grid checks passed");
}
