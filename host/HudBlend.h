#pragma once
#include <cstddef>
#include <cstdint>
#include <emmintrin.h>
namespace witchcraft {
// SSE2 is baseline on the x64 host. Source RGBA and destination BGRA are
// premultiplied; division matches the scalar compositor's integer rounding.
inline void BlendHudRow(std::uint8_t* dst,const std::uint8_t* src,std::size_t pixels) {
    const auto zero=_mm_setzero_si128(),full=_mm_set1_epi16(255),one=_mm_set1_epi16(1);
    const auto keep=_mm_set1_epi32(static_cast<int>(0xff00ff00u));
    const auto red=_mm_set1_epi32(0x000000ff),blue=_mm_set1_epi32(0x00ff0000);
    const auto alpha=_mm_set1_epi32(static_cast<int>(0xff000000u));
    std::size_t x=0;
    for(;x+4<=pixels;x+=4) {
        const auto rgba=_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+x*4));
        if(_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_and_si128(rgba,alpha),zero))==65535) continue;
        const auto bgra=_mm_or_si128(_mm_and_si128(rgba,keep),_mm_or_si128(
            _mm_slli_epi32(_mm_and_si128(rgba,red),16),_mm_srli_epi32(_mm_and_si128(rgba,blue),16)));
        const auto old=_mm_loadu_si128(reinterpret_cast<const __m128i*>(dst+x*4));
        auto blend=[&](__m128i s,__m128i d) {
            auto a=_mm_shufflelo_epi16(s,_MM_SHUFFLE(3,3,3,3));
            a=_mm_shufflehi_epi16(a,_MM_SHUFFLE(3,3,3,3));
            const auto product=_mm_mullo_epi16(d,_mm_sub_epi16(full,a));
            const auto divided=_mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(product,one),_mm_srli_epi16(product,8)),8);
            return _mm_add_epi16(s,divided);
        };
        const auto lo=blend(_mm_unpacklo_epi8(bgra,zero),_mm_unpacklo_epi8(old,zero));
        const auto hi=blend(_mm_unpackhi_epi8(bgra,zero),_mm_unpackhi_epi8(old,zero));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst+x*4),_mm_packus_epi16(lo,hi));
    }
    for(;x<pixels;++x) {
        const auto* p=src+x*4;auto* d=dst+x*4;if(!p[3]) continue;
        const int inv=255-p[3];
        d[0]=static_cast<std::uint8_t>(p[2]+d[0]*inv/255);
        d[1]=static_cast<std::uint8_t>(p[1]+d[1]*inv/255);
        d[2]=static_cast<std::uint8_t>(p[0]+d[2]*inv/255);
        d[3]=static_cast<std::uint8_t>(p[3]+d[3]*inv/255);
    }
}
}
