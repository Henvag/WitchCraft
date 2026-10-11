#define NOMINMAX
#include <Windows.h>
#include "../host/DiagnosticWall.cpp"
#include <map>
#include <tuple>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>
int main(int argc,char** argv) {
    using namespace skycraft::proto;
    if(argc!=4) return 1;
    double feet[]{std::strtod(argv[1],nullptr),std::strtod(argv[2],nullptr),std::strtod(argv[3],nullptr)};
    auto mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,kMappingName);
    if(!mapping) return 2;
    const auto* data=static_cast<const unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,kOffCollisionRing+kCollisionRingBytes));
    if(!data) return 3;
    const auto head=*reinterpret_cast<const volatile std::uint64_t*>(data+kOffCollisionRing+kColRingHeadOff);
    if(head>kColRingDataBytes) { puts("wrapped history unavailable"); return 4; }
    std::map<std::tuple<int,int,int>,std::vector<ColTri>> cubes;
    const auto* ring=data+kOffCollisionRing+kColRingDataOff;
    for(std::uint64_t offset=0;offset+sizeof(ColMsgHeader)<=head;) {
        ColMsgHeader h{}; std::memcpy(&h,ring+offset,sizeof(h));
        const auto bytes=(sizeof(h)+h.payloadBytes+7)&~7ull;
        if(bytes>head-offset||h.type>kColTris) break;
        if(h.type==kColClear) cubes.clear();
        if(h.type==kColTris&&h.payloadBytes>=sizeof(ColRegion)) {
            ColRegion r{}; std::memcpy(&r,ring+offset+sizeof(h),sizeof(r));
            if(sizeof(r)+std::uint64_t(r.count)*sizeof(ColTri)==h.payloadBytes) {
                auto& list=cubes[{r.minX,r.minY,r.minZ}]; list.resize(r.count);
                std::memcpy(list.data(),ring+offset+sizeof(h)+sizeof(r),r.count*sizeof(ColTri));
            }
        }
        offset+=bytes;
    }
    std::vector<ColTri> walls;
    std::map<int,std::tuple<double,double,double>> planes;
    for(const auto& [key,list]:cubes) for(const auto& t:list) {
        double a[3],b[3],n[3];
        for(int i=0;i<3;++i) { a[i]=t.v[i+3]-t.v[i]; b[i]=t.v[i+6]-t.v[i]; }
        n[0]=a[1]*b[2]-a[2]*b[1];n[1]=a[2]*b[0]-a[0]*b[2];n[2]=a[0]*b[1]-a[1]*b[0];
        double norm=std::hypot(n[0],n[1],n[2]); if(norm<1e-6||std::abs(n[1]/norm)>0.05) continue;
        walls.push_back(t);
        double distance=std::abs(((feet[0]-t.v[0])*n[0]+(feet[1]-t.v[1])*n[1]+(feet[2]-t.v[2])*n[2])/norm);
        double low=std::min({t.v[1],t.v[4],t.v[7]}),high=std::max({t.v[1],t.v[4],t.v[7]});
        int bin=int(distance*100);
        auto found=planes.find(bin);
        if(found==planes.end()) planes[bin]={distance,low,high};
        else {std::get<1>(found->second)=std::min(std::get<1>(found->second),low);std::get<2>(found->second)=std::max(std::get<2>(found->second),high);}
    }
    witchcraft::WallTarget target;
    printf("mesh: cubes=%zu verticalTris=%zu selected=%u feet=(%.3f,%.3f,%.3f)\n",cubes.size(),walls.size(),witchcraft::ChooseWallTarget(walls,feet,target),feet[0],feet[1],feet[2]);
    int count=0;for(const auto& [bin,p]:planes) {if(count++>=20)break; printf("plane: distance=%.3f height=%.3f..%.3f\n",std::get<0>(p),std::get<1>(p)-feet[1],std::get<2>(p)-feet[1]);}
    int bestCoverage=0;
    for(const auto& t:walls) {
        double a[3],b[3],n[3],v[3];
        for(int i=0;i<3;++i){a[i]=t.v[3+i]-t.v[i];b[i]=t.v[6+i]-t.v[i];v[i]=feet[i]-t.v[i];}
        witchcraft::Cross(a,b,n); double len=std::hypot(n[0],n[1],n[2]);
        for(double& x:n)x/=len;
        if(witchcraft::Dot(v,n)<0)for(double& x:n)x=-x;
        len=std::hypot(n[0],n[2]); n[0]/=len;n[1]=0;n[2]/=len;
        double dist=witchcraft::Dot(v,n);int covered=0;unsigned mask=0;int bit=0;
        for(double h:{0.2,0.8,1.4})for(double side:{-0.35,0.0,0.35}) {
            double p[]{feet[0]-dist*n[0]-n[2]*side,feet[1]+h,feet[2]-dist*n[2]+n[0]*side};
            bool hit=false;for(const auto& w:walls)if(witchcraft::Contains(w,p)){hit=true;break;}
            if(hit){++covered;mask|=1u<<bit;}++bit;
        }
        if(covered>bestCoverage){bestCoverage=covered;printf("coverage=%d/9 mask=%03x plane=%.3f normal=(%.5f,%.5f)\n",covered,mask,dist,n[0],n[2]);}
    }
    UnmapViewOfFile(data);CloseHandle(mapping);
}
