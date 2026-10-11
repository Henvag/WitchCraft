#include "CollisionTiles.h"
#include "TerrainVoxels.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
using namespace witchcraft;
using namespace skycraft::proto;
void Check(bool ok) { if (!ok) { puts("collision tile check failed"); std::exit(1); } }
struct Receiver:std::map<CollisionTiles::Key,std::vector<ColTri>> {
    std::set<CollisionTiles::Key> known;
    std::map<CollisionTiles::Key,std::vector<ColBlock>> voxels;
};
// Mirrors the installed receiver: only the minimum corner determines a key.
void Receive(const CollisionTiles::Packet& packet,Receiver& receiver) {
    ColRegion region{}; std::memcpy(&region,packet.bytes.data(),sizeof(region));
    Check(region.count==packet.count&&region.epoch==7);
    Check(packet.bytes.size()==sizeof(region)+region.count*(packet.type==kColRegion?sizeof(ColBlock):sizeof(ColTri)));
    const int mins[]{region.minX,region.minY,region.minZ},maxs[]{region.maxX,region.maxY,region.maxZ};
    CollisionTiles::Key key{};
    for(int axis=0;axis<3;++axis) {
        Check(mins[axis]%8==0&&maxs[axis]==mins[axis]+7);
        key[axis]=static_cast<int>(std::floor(mins[axis]/8.0));
    }
    if(packet.type==kColRegion) {
        auto& blocks=receiver.voxels[key];blocks.resize(region.count);
        if(region.count) std::memcpy(blocks.data(),packet.bytes.data()+sizeof(region),blocks.size()*sizeof(ColBlock));
        receiver.known.insert(key); return;
    }
    Check(packet.type==kColTris);
    auto& mesh=receiver[key]; mesh.resize(region.count);
    if(region.count) std::memcpy(mesh.data(),packet.bytes.data()+sizeof(region),mesh.size()*sizeof(ColTri));
}
void Drain(CollisionTiles& tiles,Receiver& receiver) {
    while(tiles.Pending()) { Receive(tiles.Front(),receiver); tiles.Accepted(); }
}
int main() {
    CollisionTiles tiles; Receiver receiver;
    const ColRegion bounds{-16,-8,-16,16,16,16,7,1};
    ColTri crossing{{7.5f,2,-0.5f,8.5f,2,-0.5f,7.5f,2,0.5f},0};
    Check(tiles.Prepare(bounds,{crossing}));
    Check(tiles.PacketCount()>4);
    Drain(tiles,receiver);
    Check(receiver.known.contains({0,0,0})&&receiver.known.contains({0,-1,0}));
    for(int x=0;x<=1;++x) for(int z=-1;z<=0;++z) {
        Check(receiver[{x,0,z}].size()==1);
        Check(!std::memcmp(&receiver[{x,0,z}][0],&crossing,sizeof(crossing)));
    }
    // A replacement has no old triangles, and old keys are explicitly emptied.
    ColTri negative{{-8,0,-8,-8,0,-7,-8,1,-8},0};
    Check(tiles.Prepare(bounds,{negative})); Drain(tiles,receiver);
    Check(receiver[{-1,0,-1}].size()==1);
    for(int x=0;x<=1;++x) for(int z=-1;z<=0;++z) Check(receiver[{x,0,z}].empty());
    Check(tiles.Prepare(bounds,{})); Drain(tiles,receiver);
    for(const auto& [key,mesh]:receiver) Check(mesh.empty());
    // Cancel after one accepted packet; a new empty patch must clean that key.
    Check(tiles.Prepare(bounds,{crossing}));
    while(tiles.Pending()&&!(tiles.Front().type==kColTris&&tiles.Front().count)) {
        Receive(tiles.Front(),receiver); tiles.Accepted();
    }
    Check(tiles.Pending());
    const auto accepted=tiles.Front().key;
    Receive(tiles.Front(),receiver); tiles.Accepted();
    const ColRegion moved{40,-8,-16,56,16,16,7,0};
    Check(tiles.Prepare(moved,{}));
    Drain(tiles,receiver); Check(receiver[accepted].empty());
    // Failed validation leaves pending work intact for the caller to fail closed.
    Check(tiles.Prepare(bounds,{crossing}));
    const auto pending=tiles.Front().bytes;
    auto invalid=crossing; invalid.v[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!tiles.Prepare(bounds,{invalid})&&tiles.Front().bytes==pending);
    invalid=crossing; invalid.v[0]=100;
    Check(!tiles.Prepare(bounds,{invalid}));
    auto tooLarge=bounds; tooLarge.maxX=1000000;
    Check(!tiles.Prepare(tooLarge,{crossing}));
    tiles.Reset(); Check(!tiles.Pending());
    // Regression: a point near Geralt must find triangles even when it lies
    // outside the eight-metre cube containing the original patch minimum.
    const ColRegion live{1060,-6,-1808,1076,6,-1792,7,1};
    const ColTri ground{{1068,2,-1801,1069,2,-1801,1068,2,-1800},0};
    Check(tiles.Prepare(live,{ground})); receiver.clear(); Drain(tiles,receiver);
    const CollisionTiles::Key player{133,0,-226};
    Check(receiver[player].size()==1);
    Check(receiver[{132,-1,-226}].empty()); // Old one-packet index could not find it here.
    // Fluids use voxel support, while the local player still uses exact triangles.
    ColTri floorA{{-1,.25f,-1,0,.25f,-1,-1,.25f,0},kTriTerrain};
    ColTri floorB{{0,.25f,-1,0,.25f,0,-1,.25f,0},kTriTerrain};
    Check(tiles.Prepare(bounds,{floorA,floorB}));Drain(tiles,receiver);
    bool support=false;
    for(const auto& block:receiver.voxels[{-1,0,-1}]) if(block.x==-1&&block.y==0&&block.z==-1) {
        Check(block.bits[0]==~std::uint64_t{0}&&block.bits[1]==~std::uint64_t{0});
        Check(block.bits[2]==0&&block.bits[7]==0);support=true;
    }
    Check(support); // groundTop is 2/8, so water cannot flow downward through this cell.
    Check(!receiver.voxels[{-1,-1,-1}].empty()); // Solid support under the measured surface.
    Check(tiles.Prepare(bounds,{}));Drain(tiles,receiver);
    for(const auto& [key,blocks]:receiver.voxels) Check(blocks.empty()); // Stale support cleared.
    auto half=TerrainVoxels(bounds,{floorA});
    bool partial=false;for(const auto& block:half) if(block.y==0) {
        Check(block.bits[0]!=~std::uint64_t{0});partial=block.bits[0]!=0;
    }
    Check(partial); // Missing terrain is not bridged into a full floor.
    Check(TerrainVoxels(bounds,{crossing}).empty()); // Untagged object/wall triangles are not filled beneath.
    puts("collision cube lookup, negative boundaries, stale removal, interruption and validation checks passed");
}
