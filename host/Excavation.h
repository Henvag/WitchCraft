#pragma once

#include "skycraft_protocol.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <vector>

// Offline excavation foundation. This does not authorize native terrain removal.
// A renderer/physics backend must commit a cut before collision may be removed.
namespace witchcraft::excavation {
using Cell = std::array<std::int32_t,3>; // Minecraft x, y (up), z.
using Bits = std::array<std::uint64_t,64>;
inline constexpr std::int32_t kMinSection=-62500, kMaxSection=62499;
inline constexpr std::size_t kMaxSections=4096, kMaxChanges=65536;

struct DugSection { Cell section{}; Bits bits{}; std::uint32_t count=0; };
enum class DecodeResult { valid, malformed, wrongWorld, outOfRange };

// RenDug is little-endian and may start at an unaligned ring address. A truncated
// nonempty packet is invalid, never a request to restore or remove terrain.
inline DecodeResult Decode(std::span<const std::uint8_t> packet,
    std::uint32_t expectedWorld,DugSection& output) {
    static_assert(std::endian::native==std::endian::little);
    using skycraft::proto::RenDug;
    if(packet.size()<sizeof(RenDug)) return DecodeResult::malformed;
    RenDug header{}; std::memcpy(&header,packet.data(),sizeof(header));
    if(!expectedWorld || header.worldId!=expectedWorld) return DecodeResult::wrongWorld;
    if(header.pad || header.count>4096 ||
        packet.size()!=sizeof(header)+(header.count?sizeof(Bits):0)) return DecodeResult::malformed;
    const Cell section{header.sx,header.sy,header.sz};
    for(auto value:section) if(value<kMinSection||value>kMaxSection) return DecodeResult::outOfRange;
    DugSection decoded{section,{},header.count};
    if(header.count) std::memcpy(decoded.bits.data(),packet.data()+sizeof(header),sizeof(Bits));
    unsigned count=0; for(auto word:decoded.bits) count+=std::popcount(word);
    if(count!=header.count) return DecodeResult::malformed;
    output=decoded;
    return DecodeResult::valid;
}

struct Changes {
    std::uint64_t revision=0;
    bool rebuildAll=false; // A full invalidation must never be silently truncated.
    std::vector<Cell> cells;
};
enum class ApplyResult { changed, unchanged, malformed, wrongWorld, outOfRange, capacity };

// Single-owner store. Transfer Changes or a copied Snapshot to other threads;
// callers must not hold references into the store across updates.
class Store {
public:
    explicit Store(std::size_t maxSections=kMaxSections,std::size_t maxChanges=kMaxChanges)
        :maxSections_(maxSections<kMaxSections?maxSections:kMaxSections),
        maxChanges_(maxChanges<kMaxChanges?maxChanges:kMaxChanges) {}
    void Reset(std::uint32_t world) {
        world_=world; sections_.clear(); count_=0;
        changes_.cells.clear(); changes_.rebuildAll=true; ++changes_.revision;
    }
    ApplyResult Apply(std::span<const std::uint8_t> packet) {
        DugSection next;
        switch(Decode(packet,world_,next)) {
        case DecodeResult::malformed:return ApplyResult::malformed;
        case DecodeResult::wrongWorld:return ApplyResult::wrongWorld;
        case DecodeResult::outOfRange:return ApplyResult::outOfRange;
        default:break;
        }
        auto found=sections_.find(next.section);
        if(found==sections_.end()&&!next.count) return ApplyResult::unchanged;
        if(found!=sections_.end()&&found->second==next.bits) return ApplyResult::unchanged;
        if(found==sections_.end()&&sections_.size()>=maxSections_) return ApplyResult::capacity;
        const Bits previous=found==sections_.end()?Bits{}:found->second;
        for(std::size_t word=0;word<previous.size();++word) {
            count_-=std::popcount(previous[word]); count_+=std::popcount(next.bits[word]);
            auto difference=previous[word]^next.bits[word];
            while(difference&&!changes_.rebuildAll) {
                if(changes_.cells.size()>=maxChanges_) {
                    changes_.cells.clear(); changes_.rebuildAll=true; break;
                }
                const unsigned bit=static_cast<unsigned>(word)*64+std::countr_zero(difference);
                difference&=difference-1;
                changes_.cells.push_back({next.section[0]*16+int(bit&15),
                    next.section[1]*16+int(bit>>8),next.section[2]*16+int((bit>>4)&15)});
            }
        }
        if(next.count) sections_[next.section]=next.bits;
        else sections_.erase(found);
        ++changes_.revision;
        return ApplyResult::changed;
    }
    bool Contains(Cell cell) const {
        Cell section{};
        for(int axis=0;axis<3;++axis) {
            const auto value=static_cast<std::int64_t>(cell[axis]);
            section[axis]=static_cast<std::int32_t>(value>=0?value/16:(value-15)/16);
        }
        const auto found=sections_.find(section);
        if(found==sections_.end()) return false;
        const unsigned x=static_cast<std::uint32_t>(cell[0])&15,
            y=static_cast<std::uint32_t>(cell[1])&15,z=static_cast<std::uint32_t>(cell[2])&15;
        const unsigned bit=x+16*z+256*y;
        return (found->second[bit/64]>>(bit%64))&1;
    }
    Changes TakeChanges() {
        Changes result=std::move(changes_);
        changes_={result.revision,false,{}};
        return result;
    }
    std::map<Cell,Bits> Snapshot() const { return sections_; }
    std::uint64_t Revision() const { return changes_.revision; }
    std::size_t Count() const { return count_; }
    std::uint32_t World() const { return world_; }
private:
    std::uint32_t world_=0;
    std::size_t count_=0,maxSections_,maxChanges_;
    std::map<Cell,Bits> sections_;
    Changes changes_;
};
}
