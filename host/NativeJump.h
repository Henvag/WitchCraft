#pragma once
#include "NativeLayout.h"
namespace witchcraft::native {
inline constexpr std::uintptr_t jumpQueueRva=0x2020090;
inline constexpr Signature jumpSignatures[]{
    {jumpQueueRva,{0x0f,0xb6,0x81,0x69,0x12,0,0,0x24,0xef,0x0c,0x0c,0x88,0x81,0x69,0x12,0,0},17},
    {jumpQueueRva+0x4b,{0xb0,0x01,0x44,0x88,0x89,0x4c,0x11,0,0,0xc3},10},
    {0x207e4a0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0xc4,0xc1,0x7b,0x10,0x01},15}};
inline bool JumpController(const Reader& reader,std::uintptr_t base,std::uintptr_t player,std::uintptr_t& out) {
    out=0;
    const auto component=reader.Pointer(player+0x218);
    const auto manager=reader.Pointer(component+0x10a0);
    const auto primary=reader.Pointer(manager+0x20);
    if(!component||!manager||!primary||reader.Pointer(component)!=base+0x383a100||
        reader.Pointer(base+0x383a100+0x638)!=base+0x2024780||
        reader.Pointer(primary)!=base+0x388c968||reader.Pointer(base+0x388c968+0x30)!=base+0x207e4a0) return false;
    for(const auto& s:jumpSignatures) {
        std::array<std::uint8_t,32> bytes{};
        for(std::size_t i=0;i<s.count;++i) if(!reader.Read(base+s.rva+i,bytes[i])) return false;
        if(std::memcmp(bytes.data(),s.bytes.data(),s.count)) return false;
    }
    out=component;return true;
}
}
