#include "NativeAdapter.h"
#include "NativeLayout.h"
#include "GameBridge.h"
#include "NativeCollision.h"
#include "CollisionTiles.h"
#include "NativePilot.h"
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

void SampleTerrain(const native::Reader& reader,const native::Sample& sample,ULONGLONG now) {
    using namespace skycraft::proto;
    if (!traceEnabled||sample.paused) return;
    auto& terrain=terrainSchedule.Patch();
    const bool newWorld=terrainWorld!=worldSequence;
    if (terrainSchedule.BeginIfNeeded(worldSequence,relocation,
        sample.feet[0],sample.feet[1],sample.feet[2],now,PlayEnabled())) {
        walls.Begin(std::floor(sample.feet[0]),std::floor(sample.feet[1]),sample.feet[2]);
        std::copy_n(sample.feet,3,wallOrigin); detailStarted=false; jobSamples=collisionSamples;
        terrainIndex=terrainHits=wallHits=0; tileDone=false; packetsPrepared=false;
        terrainStarted=now; terrainSampled=0; terrainUpdates=0;
        if (newWorld) { terrainWorld=worldSequence; clearSent=false; collisionTiles.Reset(); }
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
        auto wallMesh=walls.Triangles();
        const auto detailMesh=bodyWalls.Triangles();
        wallMesh.insert(wallMesh.end(),detailMesh.begin(),detailMesh.end());
        pendingWalls={worldSequence,terrainWorld,relocation,wallMesh};
        groundTriangles=static_cast<std::uint32_t>(triangles.size());
        wallTriangles=static_cast<std::uint32_t>(wallMesh.size());
        triangles.insert(triangles.end(),wallMesh.begin(),wallMesh.end());
        pendingOcclusion={worldSequence,terrainWorld,relocation,triangles};
        auto region=terrain.Region(terrainWorld,static_cast<std::uint32_t>(triangles.size()));
        region.maxY=static_cast<int>(std::ceil(terrain.Height()+WallPatch::top));
        if (!collisionTiles.Prepare(region,triangles)) {
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
    if (!reader.Snapshot(sample) || sample.player != reinterpret_cast<std::uintptr_t>(context)) {
        native::ApplyPilot(reader,imageBase,nullptr,worldSequence,terrainWorld,relocation,Log);
        frame.flags = kGameLoading | kGameScripted;
        WitchCraftSubmitFrame(&frame);
        if (hadSample) Log("native observer: player/camera unavailable; control remains disabled");
        hadSample = false;
        return;
    }
    if (!hadSample || previous.world != sample.world || previous.player != sample.player) {
        if (++worldSequence == 0) ++worldSequence;
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
    if(PlayEnabled()&&havePublishedPatch&&publishedWorld==worldSequence&&publishedRelocation==relocation&&
        std::abs(sample.feet[0]-publishedCentre[0])<=6&&
        std::abs(sample.feet[1]-publishedCentre[1])<=6&&std::abs(sample.feet[2]-publishedCentre[2])<=2)
        frame.flags|=kGamePlayPatch;
    if (sample.paused) frame.flags |= kGameMenu;
    frame.worldId = worldSequence; // Session-local identity, not a save binding.
    frame.relocation = relocation;
    frame.collisionEpoch = terrainWorld;
    native::ToMinecraft(sample.feet, frame.feet);
    native::ToMinecraft(sample.camera, frame.camera);
    native::ToMinecraft(sample.forward, frame.forward);
    frame.verticalFov = sample.rawFov; // Provisional; rendering is disabled.
    WitchCraftSubmitFrame(&frame);
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
