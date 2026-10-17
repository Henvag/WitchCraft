#include "CollisionTiles.h"
#include "TerrainVoxels.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace witchcraft {
bool CollisionTiles::Prepare(const skycraft::proto::ColRegion& bounds,
    const std::vector<skycraft::proto::ColTri>& triangles,const std::function<bool(int,int,int)>& dug) {
    using namespace skycraft::proto;
    const int lower[]{bounds.minX,bounds.minY,bounds.minZ};
    const int upper[]{bounds.maxX,bounds.maxY,bounds.maxZ};
    // The diagnostic exporter only produces bounded local patches. Reject
    // malformed meshes before float-to-int conversion or unbounded allocation.
    for (int axis=0;axis<3;++axis)
        if (lower[axis]<-1000032||upper[axis]>1000032||upper[axis]<lower[axis]||
            std::int64_t(upper[axis])-lower[axis]>32) return false;
    if (triangles.size()>8192) return false; // Dug ghosts and cut pieces add to the sampled 2k.
    std::map<Key,std::vector<ColTri>> tiles;
    // Empty voxel-region packets mark arrival for holdUntilReady. They do not
    // invent solid blocks. Include the whole sampled vertical range, including
    // the cube eight metres beneath the spawn point.
    Key low{},high{};
    for(int axis=0;axis<3;++axis) {
        low[axis]=static_cast<int>(std::floor(lower[axis]/double(size)));
        high[axis]=static_cast<int>(std::floor(upper[axis]/double(size)));
    }
    for(int x=low[0];x<=high[0];++x) for(int y=low[1];y<=high[1];++y)
        for(int z=low[2];z<=high[2];++z) tiles.try_emplace(Key{x,y,z});
    for (const auto& tri:triangles) {
        Key first{},last{};
        for (int axis=0;axis<3;++axis) {
            const float a=tri.v[axis],b=tri.v[3+axis],c=tri.v[6+axis];
            if (!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)) return false;
            const float lo=std::min({a,b,c}),hi=std::max({a,b,c});
            if (lo<lower[axis]-0.025f||hi>upper[axis]+0.025f||hi-lo>size) return false;
            first[axis]=static_cast<int>(std::floor(lo/size));
            last[axis]=static_cast<int>(std::floor(hi/size));
        }
        for (int x=first[0];x<=last[0];++x)
            for (int y=first[1];y<=last[1];++y)
                for (int z=first[2];z<=last[2];++z) tiles[{x,y,z}].push_back(tri);
    }
    std::map<Key,std::vector<ColBlock>> voxelTiles;
    for(const auto& block:TerrainVoxels(bounds,triangles)) {
        if(dug&&dug(block.x,block.y,block.z)) continue;
        Key key{static_cast<int>(std::floor(block.x/double(size))),
            static_cast<int>(std::floor(block.y/double(size))),static_cast<int>(std::floor(block.z/double(size)))};
        voxelTiles[key].push_back(block);
        tiles.try_emplace(key);
    }
    // An empty update removes stale triangles in a cube. Include previously
    // accepted keys even if a teleport interrupted the preceding batch.
    std::set<Key> current;
    for(const auto& [key,mesh]:tiles) current.insert(key);
    covered_=current;
    for (const auto& key:resident_) tiles.try_emplace(key);
    std::vector<Key> deliveryOrder;
    deliveryOrder.reserve(tiles.size());
    for(const auto& [key,mesh]:tiles) deliveryOrder.push_back(key);
    auto distance=[&](const Key& key) {
        double result=0;
        for(int axis=0;axis<3;++axis) {
            const double offset=key[axis]*double(size)+size*.5-
                (lower[axis]+double(upper[axis]))*.5;
            result+=offset*offset;
        }
        return result;
    };
    std::stable_sort(deliveryOrder.begin(),deliveryOrder.end(),[&](const Key& a,const Key& b) {
        const bool currentA=current.contains(a),currentB=current.contains(b);
        if(currentA!=currentB) return currentA; // Install the replacement before clearing old coverage.
        const bool geometryA=!tiles.at(a).empty(),geometryB=!tiles.at(b).empty();
        if(geometryA!=geometryB) return geometryA;
        return distance(a)<distance(b); // Deliver support near the new player position first.
    });
    std::vector<Packet> packets;
    packets.reserve(tiles.size()*2);
    for (const auto& key:deliveryOrder) {
        const auto& mesh=tiles.at(key);
        Packet packet{key,kColTris,static_cast<std::uint32_t>(mesh.size()),current.contains(key),{}};
        ColRegion region{key[0]*size,key[1]*size,key[2]*size,
            key[0]*size+size-1,key[1]*size+size-1,key[2]*size+size-1,bounds.epoch,packet.count};
        Packet marker{key,kColRegion,0,packet.retain,std::vector<std::uint8_t>(sizeof(region))};
        const auto& voxels=voxelTiles[key];
        auto markerRegion=region; markerRegion.count=static_cast<std::uint32_t>(voxels.size());
        marker.count=markerRegion.count;
        marker.bytes.resize(sizeof(region)+voxels.size()*sizeof(ColBlock));
        std::memcpy(marker.bytes.data(),&markerRegion,sizeof(markerRegion));
        if(!voxels.empty()) std::memcpy(marker.bytes.data()+sizeof(region),voxels.data(),voxels.size()*sizeof(ColBlock));
        packet.bytes.resize(sizeof(region)+mesh.size()*sizeof(ColTri));
        std::memcpy(packet.bytes.data(),&region,sizeof(region));
        if (!mesh.empty()) std::memcpy(packet.bytes.data()+sizeof(region),mesh.data(),mesh.size()*sizeof(ColTri));
        packets.push_back(std::move(packet));
        packets.push_back(std::move(marker)); // Exact triangles precede voxel support, as in SkyCraft.
    }
    pending_=std::move(packets); next_=0;
    return true;
}
void ProbeTiles::Queue(const Key& key,const std::vector<skycraft::proto::ColTri>& triangles,std::uint32_t epoch) {
    using namespace skycraft::proto;
    constexpr int size=CollisionTiles::size;
    ColRegion region{key[0]*size,key[1]*size,key[2]*size,
        key[0]*size+size-1,key[1]*size+size-1,key[2]*size+size-1,epoch,static_cast<std::uint32_t>(triangles.size())};
    Packet packet{key,kColTris,region.count,false,std::vector<std::uint8_t>(sizeof(region)+triangles.size()*sizeof(ColTri))};
    std::memcpy(packet.bytes.data(),&region,sizeof(region));
    if(!triangles.empty()) std::memcpy(packet.bytes.data()+sizeof(region),triangles.data(),triangles.size()*sizeof(ColTri));
    auto marker=region;marker.count=0;
    Packet markerPacket{key,kColRegion,0,false,std::vector<std::uint8_t>(sizeof(marker))};
    std::memcpy(markerPacket.bytes.data(),&marker,sizeof(marker));
    // A newer state of the same tile supersedes one still waiting.
    std::erase_if(pending_,[&](const Packet& p) {return p.key==key;});
    pending_.push_back(std::move(packet));pending_.push_back(std::move(markerPacket));
}
void ProbeTiles::Add(const std::vector<skycraft::proto::ColTri>& triangles,std::uint32_t epoch,
    std::uint64_t now,const std::set<Key>& covered) {
    constexpr int size=CollisionTiles::size;
    std::map<Key,std::vector<skycraft::proto::ColTri>> added;
    for(const auto& tri:triangles) {
        bool finite=true;float centre[3]{};
        for(int axis=0;axis<3;++axis) {
            for(int c=0;c<3;++c) finite&=std::isfinite(tri.v[c*3+axis])&&std::abs(tri.v[c*3+axis])<1000000;
            centre[axis]=(tri.v[axis]+tri.v[3+axis]+tri.v[6+axis])/3;
        }
        if(!finite) continue;
        Key key{};bool fits=true;
        for(int axis=0;axis<3;++axis) {
            key[axis]=static_cast<int>(std::floor(centre[axis]/size));
            for(int c=0;c<3;++c) fits&=static_cast<int>(std::floor(tri.v[c*3+axis]/size))==key[axis];
        }
        // SkyCollision files a packet under its tile only; keep triangles inside one.
        if(!fits||covered.contains(key)) continue;
        added[key].push_back(tri);
    }
    for(auto& [key,list]:added) {
        auto& tile=live_[key];
        bool changed=false;
        for(const auto& tri:list) {
            if(tile.triangles.size()>=maxPerTile) break;
            bool duplicate=false;
            for(const auto& old:tile.triangles) {
                float d=0;for(int i=0;i<9;++i) d=std::fmax(d,std::abs(old.v[i]-tri.v[i]));
                if(d<.25f) {duplicate=true;break;}
            }
            if(!duplicate) {tile.triangles.push_back(tri);changed=true;}
        }
        tile.expiry=now+lifetimeMs;
        if(changed) Queue(key,tile.triangles,epoch);
    }
}
void ProbeTiles::Expire(std::uint64_t now,std::uint32_t epoch,const std::set<Key>& covered) {
    for(auto it=live_.begin();it!=live_.end();) {
        if(covered.contains(it->first)) {it=live_.erase(it);continue;} // The patch owns it now.
        if(now<it->second.expiry) {++it;continue;}
        Queue(it->first,{},epoch);it=live_.erase(it);
    }
}
void CollisionTiles::Accepted() {
    if (!Pending()) return;
    const auto& packet=Front();
    if (packet.retain) resident_.insert(packet.key);
    else if (packet.type==skycraft::proto::kColTris) resident_.erase(packet.key);
    ++next_;
}
}
