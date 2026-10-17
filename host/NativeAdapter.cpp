#include "NativeAdapter.h"
#include "NativeLayout.h"
#include "GameBridge.h"
#include "NativeCollision.h"
#include "CollisionTiles.h"
#include "NativePilot.h"
#include "NativeCamera.h"
#include "NativeWater.h"
#include "Digging.h"
#include "ScriptBridgeNative.h"
#include "ViewDepth.h"
#include <string>
#include "Link.h"
#include "RenderTiming.h"
#include "MotionTimeline.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace witchcraft {
namespace {
using PositionFn = void (*)(void*, void*, void*);
PositionFn original = nullptr;
std::uintptr_t imageBase = 0;
void (*logger)(const char*) = nullptr;
volatile LONG observing = 0;
bool prepared = false, installed = false;
inline constexpr std::uintptr_t positionWrapper = 0x2267e50;
inline constexpr std::uintptr_t positionRecord = 0x5a5da70 + 0x8a3 * 24;
ULONGLONG lastLog = 0;
ObserverCadence observerCadence;
native::Sample previous{};
std::uint32_t worldSequence = 0, relocation = 0;
bool hadSample = false;
bool traceEnabled = false;
TerrainSchedule terrainSchedule;
WallPatch walls;
BodyWallPatch bodyWalls;
float wallOrigin[3]{};
bool detailStarted=false;
constexpr int collisionSamples=TerrainPatch::samples+WallPatch::samples;
int jobSamples=collisionSamples;
std::uint32_t terrainWorld = 0;
int terrainIndex = 0, terrainHits = 0, wallHits = 0;
std::uint32_t groundTriangles=0,wallTriangles=0;
bool clearSent = false, tileDone = false;
ULONGLONG terrainWarning = 0;
ULONGLONG terrainStarted = 0, terrainSampled = 0;
unsigned terrainUpdates = 0;
CollisionTiles collisionTiles;
ProbeTiles probeTiles;
bool packetsPrepared=false;
DiagnosticWalls pendingWalls;
DiagnosticWalls pendingOcclusion;
bool havePublishedPatch=false;
std::uint32_t publishedWorld=0,publishedRelocation=0;
float publishedCentre[3]{};
void Log(const char* text);
bool PlayEnabled() {
    static const bool enabled=[] {wchar_t v[2]{};return GetEnvironmentVariableW(L"WITCHCRAFT_PLAY",v,2)&&v[0]==L'1';}();
    return enabled;
}
// Fast movement outruns the sampled 16 m patch (and its walls sit at the
// anchor's height, not at flight altitude). Probe ahead along Minecraft's
// velocity and send what the rays hit as short-lived collision.
void ProbeAhead(const native::Reader& reader,const native::Sample& sample,ULONGLONG now) {
    using namespace skycraft::proto;
    if(!traceEnabled||!PlayEnabled()||sample.paused||terrainWorld!=worldSequence) return;
    probeTiles.Expire(now,terrainWorld,collisionTiles.Covered());
    const auto pilot=ReadPilotCommand();
    if(!pilot.active||pilot.world!=worldSequence) return;
    const auto& mc=pilot.followState;
    const double seconds=std::isfinite(mc.tickMs)&&mc.tickMs>1&&mc.tickMs<1000?mc.tickMs/1000.:.05;
    const double v[]{(mc.curX-mc.prevX)/seconds,(mc.curY-mc.prevY)/seconds,(mc.curZ-mc.prevZ)/seconds};
    const double speed=std::hypot(v[0],v[1],v[2]);
    if(!std::isfinite(speed)||speed<6||speed>150) return;
    // Witcher coordinates: X, -MC Z, MC Y.
    const float dir[]{static_cast<float>(v[0]/speed),static_cast<float>(-v[2]/speed),static_cast<float>(v[1]/speed)};
    const float length=static_cast<float>(std::fmin(40.,speed*.45+2));
    float side[]{dir[1],-dir[0],0};const float sl=std::hypot(side[0],side[1]);
    if(sl<1e-3f) {side[0]=1;side[1]=0;} else {side[0]/=sl;side[1]/=sl;}
    const float centre[]{static_cast<float>(mc.curX),static_cast<float>(-mc.curZ),static_cast<float>(mc.curY+.9)};
    const float offsets[][2]{{0,0},{.35f,0},{-.35f,0},{0,.8f},{0,-.8f}};
    std::vector<ColTri> quads;
    for(const auto& o:offsets) {
        float from[3]{},to[3]{};
        for(int i=0;i<3;++i) {from[i]=centre[i]+side[i]*o[0]+(i==2?o[1]:0);to[i]=from[i]+dir[i]*length;}
        SurfacePoint hit{};
        if(native::TraceSurface(reader,imageBase,sample,from,to,hit)!=native::TraceStatus::hit) continue;
        const float* p=hit.position;
        if(std::hypot(p[0]-sample.feet[0],p[1]-sample.feet[1])<1.2f&&p[2]>sample.feet[2]-.3f&&p[2]<sample.feet[2]+2.4f) continue;
        if(DugCell(static_cast<int>(std::floor(p[0])),static_cast<int>(std::floor(p[2])),static_cast<int>(std::floor(-p[1])))) continue;
        float n[]{hit.normal[0],hit.normal[1],hit.normal[2]};const float nl=std::hypot(n[0],n[1],n[2]);
        if(!(nl>.5f)) {for(int i=0;i<3;++i) n[i]=-dir[i];} else for(auto& c:n) c/=nl;
        // Two tangents spanning a 3 m square on the hit plane.
        float t1[]{n[1],-n[0],0};float tl=std::hypot(t1[0],t1[1]);
        if(tl<1e-3f) {t1[0]=1;t1[1]=0;tl=1;}
        for(auto& c:t1) c/=tl;
        const float t2[]{n[1]*t1[2]-n[2]*t1[1],n[2]*t1[0]-n[0]*t1[2],n[0]*t1[1]-n[1]*t1[0]};
        // 3 x 3 one-metre cells so pieces fit in collision tiles near their borders.
        for(int cu=0;cu<3;++cu) for(int cv=0;cv<3;++cv) {
            float corner[4][3]{};
            const float su[]{0,1,1,0},sv[]{0,0,1,1};
            for(int c=0;c<4;++c) {
                const float a=cu-1.5f+su[c],b=cv-1.5f+sv[c];
                float w[3]{};for(int i=0;i<3;++i) w[i]=p[i]+t1[i]*a+t2[i]*b;
                corner[c][0]=w[0];corner[c][1]=w[2];corner[c][2]=-w[1];
            }
            const int tris[][3]{{0,1,2},{0,2,3},{0,2,1},{0,3,2}}; // Both windings: two-sided.
            for(const auto& t:tris) {ColTri tri{};for(int c=0;c<3;++c) for(int i=0;i<3;++i) tri.v[c*3+i]=corner[t[c]][i];quads.push_back(tri);}
        }
    }
    if(!quads.empty()) probeTiles.Add(quads,terrainWorld,now,collisionTiles.Covered());
    for(int budget=0;budget<8&&probeTiles.Pending();++budget) {
        const auto& packet=probeTiles.Front();
        if(!WitchCraftSubmitCollision(packet.type,packet.bytes.data(),static_cast<std::uint32_t>(packet.bytes.size()))) break;
        probeTiles.Accepted();
    }
}
// Coarse scene depth along the camera's own view, refreshed every few frames.
void SampleViewDepth(const native::Reader& reader,const native::Sample& sample) {
    static ViewDepthGrid grid;static int index=-1;static std::uint32_t world=0,moved=0;
    static float anchor[3]{};
    if(!traceEnabled||!PlayEnabled()||sample.paused) return;
    if(index<0) {
        if(!grid.Begin(sample.camera,sample.forward,sample.rawFov,16.f/9)) return;
        index=0;world=worldSequence;moved=relocation;std::copy_n(sample.feet,3,anchor);
    }
    if(world!=worldSequence||moved!=relocation) {index=-1;return;}
    LARGE_INTEGER start{},frequency{},current{};
    ::QueryPerformanceCounter(&start);::QueryPerformanceFrequency(&frequency);
    for(int budget=0;budget<96&&index<ViewDepthGrid::count;++budget) {
        float from[3]{},to[3]{};grid.QueryRay(index,from,to);
        SurfacePoint surface{};
        const auto status=native::TraceSurface(reader,imageBase,sample,from,to,surface);
        if(status==native::TraceStatus::busy) break;
        bool hit=status==native::TraceStatus::hit;
        if(hit) {
            const float* p=surface.position;
            // The hidden Geralt anchor stands where Minecraft's player is drawn,
            // and dug cells are holes Minecraft draws; neither may hide anything.
            if(std::hypot(p[0]-anchor[0],p[1]-anchor[1])<1.2f&&p[2]>anchor[2]-.3f&&p[2]<anchor[2]+2.4f) hit=false;
            else if(DugCell(static_cast<int>(std::floor(p[0])),static_cast<int>(std::floor(p[2])),static_cast<int>(std::floor(-p[1])))) hit=false;
        }
        grid.Set(index++,hit,surface.position);
        ::QueryPerformanceCounter(&current);
        if((current.QuadPart-start.QuadPart)*2000>=frequency.QuadPart) break; // 0.5 ms per frame
    }
    if(index<ViewDepthGrid::count) return;
    PublishViewOcclusion({worldSequence,terrainWorld,relocation,grid.Triangles()});
    index=-1;
}
// CWorld's depot path String (TDynArray<wchar_t>: data at +0x5430, count at
// +0x5438), as read by its GetDepotPath script thunk at RVA 0x22a1190.
std::uint32_t ReadPersistentWorld(const native::Reader& reader,std::uintptr_t world) {
    if(reader.Pointer(world)!=imageBase+native::gameWorldVtable) return 0;
    std::uint32_t count=0;
    const auto data=reader.Pointer(world+0x5430);
    if(!data||!reader.Read(world+0x5438,count)||count<6||count>512) return 0;
    std::u16string path(count,u'\0');
    for(std::uint32_t i=0;i<count;++i) if(!reader.Read(data+i*2,path[i])) return 0;
    while(!path.empty()&&path.back()==u'\0') path.pop_back();
    auto hash=digging::WorldHash(path);
    if(!hash) {
        // The first live run showed an 8-bit path: read `count` bytes instead.
        path.assign(count,u'\0');
        for(std::uint32_t i=0;i<count;++i) {std::uint8_t c=0;if(!reader.Read(data+i,c)) return 0;path[i]=c;}
        while(!path.empty()&&path.back()==u'\0') path.pop_back();
        hash=digging::WorldHash(path);
    }
    char line[640]{};int used=std::snprintf(line,sizeof(line),"native world: persistent identity %08x path=",hash);
    for(const char16_t c:path) if(used<static_cast<int>(sizeof(line))-2) line[used++]=c>=0x20&&c<0x7f?static_cast<char>(c):'?';
    Log(line);
    return hash;
}
// Native lakes, rivers and sea around the Minecraft player, published as
// SkyCraft's WaterGrid so vanilla swimming, buoyancy and drowning apply there.
void SampleWater(const native::Reader& reader,const native::Sample& sample,ULONGLONG now) {
    using namespace native;
    static int verified=0; // 0 unchecked, 1 verified, -1 disabled
    static ULONGLONG nextAt=0;static std::uint32_t lastWorld=0;
    if(verified<0||sample.paused) return;
    if(lastWorld==worldSequence&&now<nextAt) return;
    nextAt=now+200;
    if(!verified) {
        std::uint8_t bytes[sizeof(waterLevelPrologue)]{};
        verified=reader.Read(imageBase+waterLevelApprox,bytes)&&!std::memcmp(bytes,waterLevelPrologue,sizeof(bytes))?1:-1;
        Log(verified>0?"native water: approximate water-level method verified":"native water: water-level method signature mismatch; swimming in native water disabled");
        if(verified<0) return;
    }
    const auto water=reader.Pointer(sample.world+worldWaterOffset);
    if(reader.Pointer(sample.world)!=imageBase+gameWorldVtable||!water||reader.Pointer(water)!=imageBase+globalWaterVtable) {
        static ULONGLONG warned=0;
        if(!warned||now-warned>=10000) {Log("native water: world or global water identity unavailable; publishing no water");warned=now;}
        const auto persistent=PersistentWorld(worldSequence);
        Link::Get().WriteWaterGrid(EmptyWaterGrid(persistent?persistent:worldSequence));lastWorld=worldSequence;return;
    }
    using LevelFn=float(*)(void*,float,float);
    const auto level=reinterpret_cast<LevelFn>(imageBase+waterLevelApprox);
    double x=sample.feet[0],z=-sample.feet[1];
    const auto pilot=ReadPilotCommand();
    if(pilot.active&&pilot.world==worldSequence&&std::isfinite(pilot.followState.curX)&&std::isfinite(pilot.followState.curZ)&&
        std::hypot(pilot.followState.curX-x,pilot.followState.curZ-z)<=32) {x=pilot.followState.curX;z=pilot.followState.curZ;}
    const auto persistent=PersistentWorld(worldSequence);
    // The global water reports the sea plane under hills too. The native anchor
    // stands on (or swims at) Witcher's own surface, so water far below it is
    // under land: drop it rather than drowning Minecraft inside the ground.
    const float landAbove=sample.feet[2]-2.5f;
    const auto grid=BuildWaterGrid(x,z,persistent?persistent:worldSequence,[&](float wx,float wy) {
        const float h=level(reinterpret_cast<void*>(water),wx,wy);
        return h<landAbove?kNativeNoWater:h;
    });
    Link::Get().WriteWaterGrid(grid);
    if(lastWorld!=worldSequence) {
        unsigned wet=0;for(const float value:grid.surface) wet+=value>-1e20f;
        char line[160]{};std::snprintf(line,sizeof(line),"native water: grid published world=%u wetColumns=%u centreLevel=%.3f",
            worldSequence,wet,grid.surface[8*16+8]);Log(line);
    }
    lastWorld=worldSequence;
}

void SampleTerrain(const native::Reader& reader,const native::Sample& sample,ULONGLONG now) {
    using namespace skycraft::proto;
    if (!traceEnabled||sample.paused) return;
    auto& terrain=terrainSchedule.Patch();
    float centre[3]{sample.feet[0],sample.feet[1],sample.feet[2]};
    const auto pilot=ReadPilotCommand();
    if(PlayEnabled()&&pilot.active&&pilot.world==worldSequence&&pilot.relocation==relocation&&
        std::isfinite(pilot.followState.curX)&&std::isfinite(pilot.followState.curZ)&&
        std::hypot(pilot.followState.curX-sample.feet[0],-pilot.followState.curZ-sample.feet[1])<=32) {
        centre[0]=static_cast<float>(pilot.followState.curX);
        centre[1]=static_cast<float>(-pilot.followState.curZ);
    }
    const bool newWorld=terrainWorld!=worldSequence;
    if (terrainSchedule.BeginIfNeeded(worldSequence,relocation,
        centre[0],centre[1],centre[2],now,PlayEnabled())) {
        walls.Begin(std::floor(centre[0]),std::floor(centre[1]),centre[2]);
        std::copy_n(centre,3,wallOrigin); detailStarted=false; jobSamples=collisionSamples;
        terrainIndex=terrainHits=wallHits=0; tileDone=false; packetsPrepared=false;
        terrainStarted=now; terrainSampled=0; terrainUpdates=0;
        if (newWorld) { terrainWorld=worldSequence; clearSent=false; collisionTiles.Reset(); probeTiles.Reset(); }
    }
    if (!clearSent) {
        if (!WitchCraftSubmitCollision(kColClear,&terrainWorld,sizeof(terrainWorld))) return;
        clearSent=true;
    }
    if (tileDone) return;
    ++terrainUpdates;
    if (!packetsPrepared&&terrainIndex<jobSamples) {
        LARGE_INTEGER start{},frequency{},current{};
        ::QueryPerformanceCounter(&start); ::QueryPerformanceFrequency(&frequency);
        // Cheap queries can finish more of the patch within the same 1 ms
        // deadline. The old 32-ray cap spread even open terrain across at least
        // 35 observer updates, allowing walking to outrun the published patch.
        for (int budget=0;budget<(PlayEnabled()?128:8)&&terrainIndex<jobSamples;++budget) {
            native::TraceStatus status;
            GroundPoint point{}; SurfacePoint surface{};
            const bool ground=terrainIndex<TerrainPatch::samples;
            if (ground) {
                float x=0,y=0; terrain.QueryPoint(terrainIndex,x,y);
                status=native::TraceGround(reader,imageBase,sample,x,y,terrain.Height(),point);
            } else {
                float from[3]{},to[3]{};
                if(terrainIndex<collisionSamples) walls.QueryRay(terrainIndex-TerrainPatch::samples,from,to);
                else bodyWalls.QueryRay(terrainIndex-collisionSamples,from,to);
                status=native::TraceSurface(reader,imageBase,sample,from,to,surface);
            }
            if (status==native::TraceStatus::invalid) {
                traceEnabled=false;
                Log("real terrain: query result validation failed; sampling disabled, control remains disabled");
                return;
            }
            if (status==native::TraceStatus::busy||status==native::TraceStatus::unavailable) {
                if (!terrainWarning||now-terrainWarning>3000) {
                    Log("real terrain: physics query unavailable/busy; retaining incomplete patch"); terrainWarning=now;
                }
                break;
            }
            if (ground) terrain.Set(terrainIndex,point);
            else if(terrainIndex<collisionSamples) walls.Set(terrainIndex-TerrainPatch::samples,surface);
            else bodyWalls.Set(terrainIndex-collisionSamples,surface);
            ++terrainIndex;
            if (status==native::TraceStatus::hit) {
                if (ground) ++terrainHits; else ++wallHits;
            }
            ::QueryPerformanceCounter(&current);
            if ((current.QuadPart-start.QuadPart)*1000>=frequency.QuadPart) break;
        }
        if(terrainIndex==collisionSamples&&!detailStarted) {
            bodyWalls.Begin(walls,wallOrigin); detailStarted=true; jobSamples+=bodyWalls.Samples();
        }
        if (terrainIndex<jobSamples) return;
        auto triangles=terrain.Triangles();
        const bool diggable=PersistentWorld(worldSequence)!=0;
        if(diggable) for(auto& tri:triangles) tri.flags=digging::DiggableTerrain();
        auto wallMesh=walls.Triangles();
        const auto detailMesh=bodyWalls.Triangles();
        wallMesh.insert(wallMesh.end(),detailMesh.begin(),detailMesh.end());
        pendingWalls={worldSequence,terrainWorld,relocation,wallMesh};
        groundTriangles=static_cast<std::uint32_t>(triangles.size());
        wallTriangles=static_cast<std::uint32_t>(wallMesh.size());
        triangles.insert(triangles.end(),wallMesh.begin(),wallMesh.end());
        if(diggable) {
            std::size_t cut=0;
            triangles=digging::CutDug(triangles,[](int x,int y,int z) {return DugCell(x,y,z);},&cut);
            static std::size_t lastCut=0;
            if(cut!=lastCut) {char line[96]{};std::snprintf(line,sizeof(line),"digging: %zu sampled triangles cut by dug cells",cut);Log(line);lastCut=cut;}
        }
        // Occlusion follows what is still there: ghosts are only for Minecraft.
        std::vector<skycraft::proto::ColTri> visible;visible.reserve(triangles.size());
        for(const auto& tri:triangles) if(!(tri.flags&skycraft::proto::kTriGhost)) visible.push_back(tri);
        pendingOcclusion={worldSequence,terrainWorld,relocation,visible};
        auto region=terrain.Region(terrainWorld,static_cast<std::uint32_t>(triangles.size()));
        region.maxY=static_cast<int>(std::ceil(terrain.Height()+WallPatch::top));
        if (!collisionTiles.Prepare(region,triangles,[](int x,int y,int z) {return DugCell(x,y,z);})) {
            traceEnabled=false;
            Log("real terrain: tile validation failed; sampling disabled, control remains disabled");
            return;
        }
        packetsPrepared=true;
        terrainSampled=::GetTickCount64();
    }
    // Never send an old pending batch after a relocation while new sampling is
    // in progress. Accepted keys remain tracked for eventual empty replacements.
    if (!packetsPrepared) return;
    // Submission only queues packets; queue capacity still applies backpressure.
    // Avoid adding a dozen observer updates just to enqueue a finished patch.
    for (int budget=0;budget<(PlayEnabled()?16:4)&&collisionTiles.Pending();++budget) {
        const auto& packet=collisionTiles.Front();
        if (!WitchCraftSubmitCollision(packet.type,packet.bytes.data(),static_cast<std::uint32_t>(packet.bytes.size()))) return;
        collisionTiles.Accepted();
    }
    if (!collisionTiles.Pending()) {
        // Keep using the previous confirmed coverage while a replacement batch
        // is in transit. Publishing new bounds waits for Minecraft consumption.
        if(PlayEnabled()&&(!GameCollisionDrained(terrainWorld)||!Link::Get().CollisionConsumed())) return;
        auto region=terrain.Region(terrainWorld,groundTriangles+wallTriangles);
        region.maxY=static_cast<int>(std::ceil(terrain.Height()+WallPatch::top));
        char line[256]{};
        std::snprintf(line,sizeof(line),"real terrain: epoch=%u rays=%d groundHits=%d wallHits=%d groundTris=%u wallTris=%u triangles=%u packets=%u region=(%d,%d,%d)-(%d,%d,%d) mode=diagnostic",
            terrainWorld,terrainIndex,terrainHits,wallHits,groundTriangles,wallTriangles,region.count,
            static_cast<unsigned>(collisionTiles.PacketCount()),
            region.minX,region.minY,region.minZ,region.maxX,region.maxY,region.maxZ);
        Log(line);
        const auto delivered=::GetTickCount64();
        std::snprintf(line,sizeof(line),"terrain delivery: updates=%u sampleMs=%llu queueAckMs=%llu totalMs=%llu",
            terrainUpdates,static_cast<unsigned long long>(terrainSampled-terrainStarted),
            static_cast<unsigned long long>(delivered-terrainSampled),
            static_cast<unsigned long long>(delivered-terrainStarted));
        Log(line);
        havePublishedPatch=true; publishedWorld=worldSequence; publishedRelocation=relocation;
        PublishDiagnosticWalls(pendingWalls);
        PublishOcclusionGeometry(pendingOcclusion);
        publishedCentre[0]=region.minX+TerrainPatch::cells/2;
        publishedCentre[1]=-region.maxZ+TerrainPatch::cells/2;
        publishedCentre[2]=terrain.Height();
        tileDone=true; terrainSchedule.Published(now);
    }
}

bool ReadMemory(void*, std::uintptr_t address, void* out, std::size_t bytes) {
    SIZE_T received = 0;
    return address && ::ReadProcessMemory(::GetCurrentProcess(),
        reinterpret_cast<const void*>(address), out, bytes, &received) && received == bytes;
}
void Log(const char* text) { if (logger) logger(text); }

void Observe(void* context) {
    if (::InterlockedCompareExchange(&observing, 1, 0)) return;
    struct Release { ~Release() { ::InterlockedExchange(&observing, 0); } } release;
    const auto now = ::GetTickCount64();
    LARGE_INTEGER clock{},frequency{};
    ::QueryPerformanceCounter(&clock);::QueryPerformanceFrequency(&frequency);
    if(!observerCadence.Due(clock.QuadPart,frequency.QuadPart)) return;
    static std::int64_t cadenceAt=clock.QuadPart,previousAt=0,peakGap=0;
    static unsigned cadenceSamples=0;
    if(previousAt) peakGap=std::max(peakGap,clock.QuadPart-previousAt);
    previousAt=clock.QuadPart;++cadenceSamples;
    if(clock.QuadPart-cadenceAt>=frequency.QuadPart*5) {
        char timing[160]{};
        std::snprintf(timing,sizeof(timing),"native cadence: samplesPerSecond=%.1f peakGapMs=%.2f",
            cadenceSamples*static_cast<double>(frequency.QuadPart)/(clock.QuadPart-cadenceAt),
            peakGap*1000.0/frequency.QuadPart);
        Log(timing);
        const auto motion=ReadMotionStats();
        if(motion.renderFrames) {
            char line[160]{};std::snprintf(line,sizeof(line),
                "motion timeline: rendered=%u heldMoving=%u bufferMs=%.2f",
                motion.renderFrames,motion.heldMovingFrames,motion.bufferMs);Log(line);
        }
        cadenceAt=clock.QuadPart;cadenceSamples=0;peakGap=0;
    }
    native::Reader reader(imageBase, ReadMemory, nullptr);
    native::Sample sample{};
    GameFrame frame{};
    static ULONGLONG completeCameraAt=0;
    bool sampled=reader.Snapshot(sample);
    if(sampled) completeCameraAt=now;
    else if(hadSample&&completeCameraAt&&reader.Snapshot(sample,false))
        sampled=native::ReuseCamera(sample,previous,now-completeCameraAt);
    if (!sampled || sample.player != reinterpret_cast<std::uintptr_t>(context)) {
        native::ApplyPilot(reader,imageBase,nullptr,worldSequence,terrainWorld,relocation,Log);
        frame.flags = kGameLoading | kGameScripted;
        WitchCraftSubmitFrame(&frame);
        if (hadSample) Log("native observer: player/camera unavailable; control remains disabled");
        hadSample = false;
        return;
    }
    if (!hadSample || previous.world != sample.world || previous.player != sample.player) {
        if (++worldSequence == 0) ++worldSequence;
        const auto persistent=ReadPersistentWorld(reader,sample.world);
        if(!persistent) Log("native world: persistent identity unavailable; digging into Witcher terrain disabled");
        PublishPersistentWorld(worldSequence,persistent);
        if (++relocation == 0) ++relocation;
    } else {
        double distanceSquared = 0;
        for (int i = 0; i < 3; ++i) {
            const double delta = sample.feet[i] - previous.feet[i];
            distanceSquared += delta * delta;
        }
        if (distanceSquared > 400 && ++relocation == 0) ++relocation;
    }
    // Diagnostic phase: projection semantics, scene state and collision have
    // not been verified. This explicit handoff prevents Session from enabling
    // input capture, rendering or Minecraft physics, regardless of MC state.
    SampleTerrain(reader,sample,now);
    frame.flags = kGamePlaying | kGameScripted;
    if (havePublishedPatch&&publishedWorld==worldSequence&&publishedRelocation==relocation&&
        std::abs(sample.feet[0]-publishedCentre[0])<=4&&
        std::abs(sample.feet[1]-publishedCentre[1])<=4&&std::abs(sample.feet[2]-publishedCentre[2])<=2)
        frame.flags|=kGameDiagnosticPatch;
    const auto pilot=ReadPilotCommand();
    const double coverageX=pilot.active?pilot.followState.curX:sample.feet[0];
    const double coverageY=pilot.active?-pilot.followState.curZ:sample.feet[1];
    if(PlayEnabled()&&havePublishedPatch&&publishedWorld==worldSequence&&publishedRelocation==relocation&&
        std::abs(coverageX-publishedCentre[0])<=6&&
        std::abs(coverageY-publishedCentre[1])<=6&&std::abs(sample.feet[2]-publishedCentre[2])<=2)
        frame.flags|=kGamePlayPatch;
    if (sample.paused) frame.flags |= kGameMenu;
    frame.worldId = worldSequence; // Session-local identity, not a save binding.
    frame.relocation = relocation;
    frame.collisionEpoch = terrainWorld;
    native::ToMinecraft(sample.feet, frame.feet);
    native::ToMinecraft(sample.camera, frame.camera);
    float flipped[3]{};
    if(native::FrontViewForward(flipped,now)&&
        sample.forward[0]*flipped[0]+sample.forward[1]*flipped[1]+sample.forward[2]*flipped[2]>.995f) {
        // The director built the front camera; Minecraft keeps looking the other way.
        const float look[]{-sample.forward[0],-sample.forward[1],-sample.forward[2]};
        native::ToMinecraft(look, frame.forward);
        frame.flags|=kGameFrontView;
    } else native::ToMinecraft(sample.forward, frame.forward);
    frame.verticalFov = sample.rawFov; // Provisional; rendering is disabled.
    WitchCraftSubmitFrame(&frame);
    SampleWater(reader,sample,now);
    SampleViewDepth(reader,sample);
    ProbeAhead(reader,sample,now);
    script::InstallScriptBridge(imageBase,Log);
    native::ApplyPilot(reader,imageBase,&sample,worldSequence,terrainWorld,relocation,Log);
    if (!hadSample || now - lastLog >= 3000) {
        char line[384]{};
        std::snprintf(line, sizeof(line),
            "native observer: thread=%lu world=%u paused=%u feet=(%.3f,%.3f,%.3f) camera=(%.3f,%.3f,%.3f) forward=(%.4f,%.4f,%.4f) rawFov=%.3f mode=observe",
            ::GetCurrentThreadId(), worldSequence, sample.paused ? 1u : 0u,
            sample.feet[0], sample.feet[1], sample.feet[2],
            sample.camera[0], sample.camera[1], sample.camera[2],
            sample.forward[0], sample.forward[1], sample.forward[2], sample.rawFov);
        Log(line);
        lastLog = now;
    }
    previous = sample;
    hadSample = true;
}

void PositionCallback(void* context, void* stack, void* result) {
    original(context, stack, result);
    // This wrapper also serves NPCs. Observe only calls on the verified player.
    native::Reader reader(imageBase, ReadMemory, nullptr);
    if (reader.Pointer(reinterpret_cast<std::uintptr_t>(context)) == imageBase + native::playerVtable)
        Observe(context);
}
}

bool StartNativeAdapter(void (*log)(const char*)) {
    logger = log;
    wchar_t disabled[2]{};
    if (::GetEnvironmentVariableW(L"WITCHCRAFT_DISABLE_NATIVE", disabled, 2) && disabled[0] == L'1') {
        Log("native observer disabled by WITCHCRAFT_DISABLE_NATIVE");
        return false;
    }
    imageBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
    native::Reader reader(imageBase, ReadMemory, nullptr);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!reader.Read(imageBase, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !reader.Read(imageBase + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != native::timestamp ||
        nt.OptionalHeader.SizeOfImage != native::imageSize || !reader.ValidateCode()) {
        Log("native observer: executable fingerprint/signatures differ; no engine hook installed");
        return false;
    }
    // The host worker and trampoline have process lifetime. Pin this DLL so
    // FreeLibrary cannot leave a live engine callback in unmapped memory.
    HMODULE pinned = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&StartNativeAdapter), &pinned)) {
        Log("native observer: DLL pin failed; no hook installed");
        return false;
    }
    original = reinterpret_cast<PositionFn>(imageBase + positionWrapper);
    traceEnabled=native::ValidateTraceCode(reader,imageBase);
    wchar_t terrainDisabled[2]{};
    if (::GetEnvironmentVariableW(L"WITCHCRAFT_DISABLE_TERRAIN",terrainDisabled,2)&&terrainDisabled[0]==L'1')
        traceEnabled=false;
    Log(traceEnabled?"real terrain sampler prepared: ray-query signature verified; gameplay remains disabled":
        "real terrain sampler disabled: ray-query signature mismatch");
    prepared = true;
    Log("native observer prepared: seven code signatures verified; waiting for native registration");
    PollNativeAdapter();
    return true;
}

void PollNativeAdapter() {
    if (!prepared || installed) return;
    native::Reader reader(imageBase, ReadMemory, nullptr);
    const auto slot = imageBase + positionRecord;
    const auto expected = reinterpret_cast<void*>(original);
    std::int32_t adjustment = 0;
    if (reader.Pointer(slot) != reinterpret_cast<std::uintptr_t>(expected) ||
        !reader.Read(slot + 8, adjustment) || adjustment != 0) return;
    MEMORY_BASIC_INFORMATION region{};
    if (!::VirtualQuery(reinterpret_cast<void*>(slot), &region, sizeof(region)) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(region.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) return;
    // Preserve engine registration and any other mod's hook. The dispatch
    // pointer is naturally aligned and exchanged atomically; no code patch or
    // instruction relocation is necessary for the AVX getter wrapper.
    const auto found = ::InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot),
        reinterpret_cast<void*>(&PositionCallback), expected);
    if (found != expected) return;
    installed = true;
    Log("native observer installed: GetWorldPosition dispatch callback; movement disabled");
}
}
