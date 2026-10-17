#include "Excavation.h"
#include "ExcavationClip.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace witchcraft::excavation;
using skycraft::proto::RenDug;
void Check(bool value,const char* reason) {
    if(!value) {std::printf("excavation failure: %s\n",reason);std::exit(1);}
}
std::vector<std::uint8_t> Packet(Cell section,std::uint32_t world,const Bits& bits) {
    unsigned count=0;for(auto word:bits)count+=std::popcount(word);
    RenDug header{section[0],section[1],section[2],count,world,0};
    std::vector<std::uint8_t> result(sizeof(header)+(count?sizeof(bits):0));
    std::memcpy(result.data(),&header,sizeof(header));
    if(count)std::memcpy(result.data()+sizeof(header),bits.data(),sizeof(bits));
    return result;
}
double Area(const CutResult& cut) {
    double result=0;for(const auto& t:cut.triangles)result+=std::sqrt(detail::AreaSquared(t))/2;
    return result;
}
int main() {
    Store store(2,4);store.Reset(17);
    Check(store.TakeChanges().rebuildAll,"world change invalidates native cuts");
    Bits bits{};bits[0]=1;bits[63]=1ull<<63;
    const auto packet=Packet({-1,-2,3},17,bits);
    // Negative coordinates, high bit and unaligned input exercise protocol order.
    std::vector<std::uint8_t> unaligned(1,0);unaligned.insert(unaligned.end(),packet.begin(),packet.end());
    Check(store.Apply(std::span(unaligned).subspan(1))==ApplyResult::changed,"unaligned decode");
    Check(store.Contains({-16,-32,48})&&store.Contains({-1,-17,63}),"x + 16z + 256y and negative sections");
    Check(!store.Contains({-17,-32,48})&&!store.Contains({-16,-31,48}),"no adjacent coordinate alias");
    Check(store.Count()==2,"count derives from bits");
    auto changes=store.TakeChanges();
    Check(changes.cells.size()==2&&!changes.rebuildAll,"changed cells exported");
    const auto revision=store.Revision();
    Check(store.Apply(packet)==ApplyResult::unchanged&&store.Revision()==revision,"resends are idempotent");
    auto shortPacket=packet;shortPacket.pop_back();
    Check(store.Apply(shortPacket)==ApplyResult::malformed&&store.Count()==2,"truncation cannot clear terrain");
    auto badCount=packet;badCount[12]=3;
    Check(store.Apply(badCount)==ApplyResult::malformed&&store.Revision()==revision,"count mismatch cannot mutate");
    auto badPad=packet;badPad[20]=1;
    Check(store.Apply(badPad)==ApplyResult::malformed,"reserved bytes validated");
    Check(store.Apply(Packet({0,0,0},18,bits))==ApplyResult::wrongWorld,"old-world packet rejected");
    Check(store.Apply(Packet({kMaxSection+1,0,0},17,bits))==ApplyResult::outOfRange,"coordinates cannot overflow or alias");
    Check(store.Apply(Packet({2,0,0},17,bits))==ApplyResult::changed,"second section");
    Check(store.Apply(Packet({3,0,0},17,bits))==ApplyResult::capacity&&store.Count()==4,"bounded storage");
    Check(store.Apply(Packet({-1,-2,3},17,{}))==ApplyResult::changed&&!store.Contains({-16,-32,48}),"explicit clear");
    auto clearTrailing=Packet({2,0,0},17,{});clearTrailing.push_back(0);
    Check(store.Apply(clearTrailing)==ApplyResult::malformed&&store.Count()==2,"clear with trailing bits rejected");
    store.TakeChanges();
    Bits many{};many[0]=0xff;
    Check(store.Apply(Packet({2,0,0},17,many))==ApplyResult::changed,"large changed set");
    changes=store.TakeChanges();
    Check(changes.rebuildAll&&changes.cells.empty(),"overflow requires full rebuild rather than losing cells");
    store.Reset(19);
    Check(!store.Count()&&store.Snapshot().empty()&&store.TakeChanges().rebuildAll,"world reset drops old holes");
    Check(store.Apply(packet)==ApplyResult::wrongWorld,"late packets cannot resurrect old cuts");

    const std::array<Position,3> ground{{{-2,0,-2},{2,0,-2},{-2,0,2}}};
    const std::array<Cell,1> hole{{{-1,-1,-1}}};
    const auto cut=CutTriangle(ground,hole);
    Check(cut.status==CutResult::complete&&std::abs(Area(cut)-7)<.002,"unit excavation removes one square metre");
    for(const auto& triangle:cut.triangles) {
        for(const auto& vertex:triangle) {
            double sum=0;for(auto weight:vertex.weights) {Check(weight>=-1e-12,"weights stay inside source triangle");sum+=weight;}
            Check(std::abs(sum-1)<1e-12,"barycentric weights normalized");
            for(int axis=0;axis<3;++axis) {
                double value=0;for(int corner=0;corner<3;++corner)value+=vertex.weights[corner]*ground[corner][axis];
                Check(std::abs(value-vertex.position[axis])<1e-10,"native vertex attributes can be interpolated");
            }
        }
        const auto& a=triangle[0].position;const auto& b=triangle[1].position;const auto& c=triangle[2].position;
        Check((b[0]-a[0])*(c[2]-a[2])-(b[2]-a[2])*(c[0]-a[0])>0,"triangle winding preserved");
        const double x=(a[0]+b[0]+c[0])/3,z=(a[2]+b[2]+c[2])/3;
        Check(x<=-1||x>=0||z<=-1||z>=0,"no retained triangle centroid inside hole");
    }
    const std::array<Cell,1> far{{{100,100,100}}};
    Check(std::abs(Area(CutTriangle(ground,far))-8)<1e-12,"unaffected triangle remains intact");
    const std::array<Cell,2> duplicate{{hole[0],hole[0]}};
    Check(std::abs(Area(CutTriangle(ground,duplicate))-Area(cut))<1e-10,"repeated dug cells do not add holes");
    const std::array<Position,3> buried{{{-.8,0,-.8},{-.2,0,-.8},{-.8,0,-.2}}};
    Check(CutTriangle(buried,hole).triangles.empty(),"surface on top boundary belongs to removed cell");
    const std::array<Position,3> wall{{{0,-2,-2},{0,2,-2},{0,-2,2}}};
    const std::array<Cell,1> wallHole{{{-1,-1,-1}}};
    Check(std::abs(Area(CutTriangle(wall,wallHole))-7)<.002,"vertical wall uses same clipping");
    auto invalid=ground;invalid[0][0]=std::numeric_limits<double>::quiet_NaN();
    Check(CutTriangle(invalid,hole).status==CutResult::invalid,"invalid source geometry rejected");
    const auto capped=CutTriangle(ground,hole,1);
    Check(capped.status==CutResult::budgetExceeded&&capped.triangles.empty(),"budget failure never yields partial collision removal");
    std::vector<Cell> excessive(257,hole[0]);
    Check(CutTriangle(ground,excessive).status==CutResult::budgetExceeded,"bounded number of cuts");
    puts("excavation protocol, state and triangle clipping checks passed");
}
