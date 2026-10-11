#include "NativeCollision.h"
#include <cmath>
#include <cstring>

namespace witchcraft::native {
namespace {
inline constexpr std::uintptr_t physicsVtable = 0x3828068, rayFunction = 0x1e965e0;
struct alignas(16) Vec4 { float x,y,z,w; };
struct alignas(16) TraceResult {
    Vec4 position{}, normal{};
    float fraction=0;
    std::int32_t unknown36=-1, unknown40=-1, pad44=0;
    std::uintptr_t component=0, unknown56=0, optionalMatrix64=0, optionalMatrix72=0, unknown80=0;
};
static_assert(offsetof(TraceResult, normal)==16 && offsetof(TraceResult, component)==48);
static_assert(sizeof(TraceResult)==96);
using RayFn=int (*)(void*,const Vec4*,const Vec4*,std::uint64_t,std::uint64_t,TraceResult*);

bool NameEquals(const Reader& r,std::uintptr_t base,std::uint32_t id,const char* wanted) {
    if (id>300000) return false;
    const auto pool=r.Pointer(base+0x5857dd8);
    const auto names=pool?r.Pointer(pool+0x11830):0;
    const auto holder=names?r.Pointer(names+8ull*id):0;
    const auto text=holder?r.Pointer(holder):0;
    if (!text) return false;
    // Read only through the terminating byte, not beyond a short name allocation.
    for (std::size_t i=0;i<32;++i) {
        char c=0;
        if (!r.Read(text+i,c)||c!=wanted[i]) return false;
        if (!c) return true;
    }
    return false;
}
bool GroupMask(const Reader& r,std::uintptr_t base,std::uint64_t& mask) {
    const auto groups=r.Pointer(base+0x5a58ec0);
    std::uint32_t count=0;
    if (!groups||!r.Read(groups+0x10,count)||count>48) return false;
    const auto names=r.Pointer(groups+8);
    bool terrain=false,statics=false;
    mask=0;
    for (unsigned i=0;i<count;++i) {
        std::uint32_t id=0;
        if (!names||!r.Read(names+i*4,id)) return false;
        const bool t=NameEquals(r,base,id,"Terrain"), s=NameEquals(r,base,id,"Static");
        if (t||s) mask|=1ull<<i;
        terrain|=t; statics|=s;
    }
    return terrain&&statics;
}
struct MaskCache {
    std::uintptr_t world=0,groups=0,names=0;
    std::uint32_t count=0;
    std::uint64_t mask=0;
};
thread_local MaskCache cached;
}
bool ValidateTraceCode(const Reader& r,std::uintptr_t base) {
    const unsigned char expected[]{0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x55,
        0x48,0x8d,0x6c,0x24,0x80,0x48,0x81,0xec,0x80,0x01,0,0};
    unsigned char actual[sizeof(expected)]{};
    return r.Read(base+rayFunction,actual)&&!std::memcmp(actual,expected,sizeof(expected));
}
TraceStatus TraceSurface(const Reader& r,std::uintptr_t base,const Sample& sample,
    const float* start,const float* end,SurfacePoint& result) {
    result={};
    const auto physics=r.Pointer(sample.world+0x1f0);
    const auto vt=physics?r.Pointer(physics):0;
    if (vt!=base+physicsVtable||r.Pointer(vt+8)!=base+rayFunction) return TraceStatus::unavailable;
    const auto groups=r.Pointer(base+0x5a58ec0);
    const auto names=groups?r.Pointer(groups+8):0;
    std::uint32_t count=0;
    if (!groups||!r.Read(groups+0x10,count)) return TraceStatus::unavailable;
    if (cached.world!=sample.world||cached.groups!=groups||cached.names!=names||cached.count!=count) {
        std::uint64_t mask=0;
        if (!GroupMask(r,base,mask)) return TraceStatus::unavailable;
        cached={sample.world,groups,names,count,mask};
    }
    const Vec4 from{start[0],start[1],start[2],1},to{end[0],end[1],end[2],1};
    TraceResult hit{};
    // Identical six-argument call to CWorld.StaticTrace's underlying query.
    // Component is a borrowed pointer; no output ownership is transferred.
    const auto status=reinterpret_cast<RayFn>(base+rayFunction)(reinterpret_cast<void*>(physics),&from,&to,cached.mask,0,&hit);
    if (status==0) return TraceStatus::miss;
    if (status==2) return TraceStatus::busy;
    if (status!=1) return TraceStatus::invalid;
    const auto& p=hit.position; const auto& n=hit.normal;
    result={{p.x,p.y,p.z},{n.x,n.y,n.z},true};
    if (!ValidSurfaceHit(start,end,result)) { result={}; return TraceStatus::invalid; }
    return TraceStatus::hit;
}
TraceStatus TraceGround(const Reader& r,std::uintptr_t base,const Sample& sample,
    float x,float y,float height,GroundPoint& result) {
    result={x,y,0,0,false};
    const float from[]{x,y,height+2},to[]{x,y,height-8};
    SurfacePoint surface{};
    const auto status=TraceSurface(r,base,sample,from,to,surface);
    if (status==TraceStatus::hit) result={surface.position[0],surface.position[1],surface.position[2],surface.normal[2],true};
    return status;
}
}
