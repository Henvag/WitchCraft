#pragma once
#include "NativeJump.h"
namespace witchcraft::native {
using GravityWrite=bool(*)(std::uintptr_t,const void*,std::size_t);
inline bool JumpPhysics(const Reader& reader,std::uintptr_t base,std::uintptr_t player,std::uintptr_t& out) {
    out=0;std::uintptr_t component=0;
    if(reader.Pointer(player)!=base+playerVtable) return false;
    if(!JumpController(reader,base,player,component)) return false;
    const auto primary=reader.Pointer(reader.Pointer(component+0x10a0)+0x20);
    const auto physical=reader.Pointer(primary+0x10);
    const auto wrapper=reader.Pointer(component+0x16d8);
    if(!physical||!wrapper||reader.Pointer(wrapper+0x10)!=physical||reader.Pointer(physical)!=base+0x39b32f0) return false;
    // SetGravity's inline operation: preserve all but bit 0x10, reset velocity.
    constexpr std::uint8_t expected[]{0x0f,0xb6,0x81,0xc4,0x02,0,0,0x41,0x80,0xe1,0x01,0x24,0xef};
    for(std::size_t i=0;i<sizeof(expected);++i) {std::uint8_t b=0;if(!reader.Read(base+0x1fe1531+i,b)||b!=expected[i]) return false;}
    out=physical;return true;
}
class JumpGravity {
    std::uintptr_t player_=0,physical_=0;
public:
    bool Owned() const {return physical_!=0;}
    bool Acquire(const Reader& reader,std::uintptr_t base,std::uintptr_t player,GravityWrite write) {
        std::uintptr_t physical=0;
        if(!JumpPhysics(reader,base,player,physical)) return false;
        std::uint8_t flags=0;if(!reader.Read(physical+0x2c4,flags)) return false;
        if(Owned()) return player==player_&&physical==physical_&&!(flags&0x10);
        if(!(flags&0x10)) return false; // Do not borrow an existing game-owned state.
        const float zero[4]{};
        if(!write(physical+0x140,zero,sizeof(zero))) return false;
        player_=player;physical_=physical; // Retain ownership through partial failure.
        const std::uint8_t disabled=flags&~0x10;
        if(!write(physical+0x2c4,&disabled,sizeof(disabled))) return false;
        return reader.Read(physical+0x2c4,flags)&&!(flags&0x10);
    }
    bool Restore(const Reader& reader,std::uintptr_t base,GravityWrite write) {
        if(!Owned()) return true;
        std::uintptr_t physical=0;
        if(!JumpPhysics(reader,base,player_,physical)||physical!=physical_) return false;
        std::uint8_t flags=0;if(!reader.Read(physical+0x2c4,flags)) return false;
        const float zero[4]{};
        if(!write(physical+0x140,zero,sizeof(zero))) return false;
        const std::uint8_t restored=flags|0x10;
        if(!write(physical+0x2c4,&restored,sizeof(restored))||!reader.Read(physical+0x2c4,flags)||!(flags&0x10)) return false;
        player_=physical_=0;return true;
    }
};
}
