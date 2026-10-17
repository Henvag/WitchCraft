#include "NativePilot.h"
#include "NativeCamera.h"
#include "Link.h"
#include "MotionTimeline.h"

#include <Windows.h>
#include <cstdio>
#include <atomic>
namespace witchcraft::native {
namespace {
std::uintptr_t hiddenPlayer=0;
PilotStatus status;
PilotMotion motion;
PilotRetryCadence retryCadence;
unsigned requests=0;
std::uint64_t lastRequestLog=0;
std::uint64_t lastRehideLog=0;
std::atomic<bool> recoveryRequested=false;
constexpr std::uintptr_t hideRva=0x1fd57c0,teleportRva=0x1e2fc80;
using HideFn=void(*)(void*,bool,bool,std::uint16_t);
using TeleportFn=bool(*)(void*,const float*,const float*);
bool Validate(const Reader& reader,std::uintptr_t base,std::uintptr_t player) {
    if(reader.Pointer(player)!=base+playerVtable||reader.Pointer(base+playerVtable+0x430)!=base+hideRva||
        reader.Pointer(base+playerVtable+0x460)!=base+teleportRva) return false;
    const Signature expected[]{
        {hideRva,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x70},20},
        {teleportRva,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xe8,0x8e,0x3b,0x1a,0},18},
        {0x1fd3820,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20},20}};
    for(const auto& s:expected) {
        std::array<std::uint8_t,32> bytes{};
        if(!reader.Read(base+s.rva,bytes)) return false;
        if(std::memcmp(bytes.data(),s.bytes.data(),s.count)) return false;
    }
    return true;
}
}
void RequestPilotRecovery() {recoveryRequested=true;}
void ApplyPilot(const Reader& reader,std::uintptr_t base,const Sample* sample,
    std::uint32_t world,std::uint32_t epoch,std::uint32_t relocation,void(*log)(const char*)) {
    static const bool enabled=[] { wchar_t v[2]{};
        if(GetEnvironmentVariableW(L"WITCHCRAFT_PLAY",v,2)&&v[0]==L'1') return true;
        if(GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_PILOT",v,2)&&v[0]==L'1') return true;
        if(GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_MANUAL",v,2)&&v[0]==L'1') return true;
        if(GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_JUMP",v,2)&&v[0]==L'1') return true;
        return GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_MOVEMENT",v,2)&&v[0]==L'1'; }();
    if(!enabled) return;
    PrepareCamera(reader,base,sample,log);
    static const bool playEnabled=[] {wchar_t v[2]{};return GetEnvironmentVariableW(L"WITCHCRAFT_PLAY",v,2)&&v[0]==L'1';}();
    static const bool jumpEnabled=[] {wchar_t v[2]{};return GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_JUMP",v,2)&&v[0]==L'1';}();
    const auto command=ReadPilotCommand();
    const auto now=GetTickCount64();
    if(recoveryRequested&&sample&&!command.active&&!hiddenPlayer&&!status.hidden) {
        recoveryRequested=false;
        std::uint16_t reasons=0;
        if(Validate(reader,base,sample->player)&&reader.Read(sample->player+0x30c,reasons)&&!reasons) {
            motion.Reset();retryCadence.Reset();status.failed=false;PublishPilotStatus(status);
            log("native pilot: F8 recovery revalidated player methods and restored visibility; fresh handoff allowed");
        } else log("native pilot: F8 recovery rejected; player identity or visibility remains incompatible");
    }
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool active=command.active&&!status.failed&&sample&&!sample->paused&&foreground==GetCurrentProcessId()&&
        command.world==world&&command.epoch==epoch&&command.relocation==relocation&&
        (!playEnabled||(PilotCommandFresh(command,now)&&Link::Get().McAlive()));
    auto restore=[&] {
        motion.Reset();
        retryCadence.Reset();requests=0;lastRequestLog=lastRehideLog=0;
        if(!hiddenPlayer) return;
        if(Validate(reader,base,hiddenPlayer)) {
            reinterpret_cast<HideFn>(base+hideRva)(reinterpret_cast<void*>(hiddenPlayer),false,false,kPilotHideReason);
            std::uint16_t remaining=0;
            if(reader.Read(hiddenPlayer+0x30c,remaining)&&!(remaining&kPilotHideReason))
                log("native pilot: owned hide reason removal confirmed");
            else {status.failed=true;log("native pilot: hide reason removal not confirmed; trial disabled");}
        } else log("native pilot: previous player unavailable; no stale object call during restore");
        hiddenPlayer=0;status.hidden=false;PublishPilotStatus(status);
    };
    if(!active) {restore();return;}
    if(hiddenPlayer&&hiddenPlayer!=sample->player) restore();
    auto fail=[&](const char* why) {status.failed=true;log(why);restore();PublishPilotStatus(status);};
    if(!Validate(reader,base,sample->player)) {fail("native pilot: method identity/signature mismatch; stopped");return;}
    const auto ack=motion.Observe(sample->feet,GetTickCount64());
    const bool continuous=playEnabled&&command.cameraOwned;
    if(ack==PilotAck::TimedOut&&!continuous) {
        char line[200]{};std::snprintf(line,sizeof(line),"native pilot: position acknowledgement timed out; closestError=%.3f actual=(%.3f,%.3f,%.3f)",motion.MinimumError(),sample->feet[0],sample->feet[1],sample->feet[2]);log(line);
        fail("native pilot: requested position not observed within 250 ms; stopped");return;
    }
    if(ack==PilotAck::Waiting&&!continuous) return;
    // Retarget from the latest observed anchor each update. Steps of up to 2 m
    // per callback keep pace with Elytra and sprint flight at 60 Hz. Native locomotion
    // may move after an accepted teleport, so waiting 250 ms for the obsolete
    // exact target stalls follow and makes Minecraft outrun terrain delivery.
    if(continuous) motion.Reset();
    if(ack==PilotAck::Confirmed) {
        ++status.moves;
        if(jumpEnabled||status.moves<=3) {char line[200]{};std::snprintf(line,sizeof(line),"native pilot: follow confirmed on later callback #%u feet=(%.3f,%.3f,%.3f) closestError=%.3f",status.moves,sample->feet[0],sample->feet[1],sample->feet[2],motion.MinimumError());log(line);}
        PublishPilotStatus(status);
    }
    std::uint16_t reasons=0;
    if(!reader.Read(sample->player+0x30c,reasons)) {fail("native pilot: hide state unavailable; stopped");return;}
    const auto visibility=PilotVisibilityFor(hiddenPlayer!=0,reasons);
    if(visibility==PilotVisibility::GameOwned) {fail("native pilot: player already hidden by game; trial stopped");return;}
    alignas(16) float destination[4]{},rotation[4]{};
    LARGE_INTEGER followNow{},followFrequency{};
    QueryPerformanceCounter(&followNow);QueryPerformanceFrequency(&followFrequency);
    double target[3]{};
    // Evaluate the tick interpolation at consumption, not at the preceding
    // overlay upload. This removes an extra stale-pose delay without prediction.
    auto followCommand=command;
    if(playEnabled&&command.groundFollow&&Link::Get().McAlive()) {
        skycraft::proto::McState latest{};
        if(Link::Get().ReadMcState(latest)) UseLatestFollowState(followCommand,latest);
    }
    // The measured native-to-camera delay is about one frame. Advance only
    // along the known horizontal tick segment to offset that camera delay.
    ResolvePilotFeet(followCommand,followNow.QuadPart,followFrequency.QuadPart,target,
        playEnabled?1000.0/60:0);
    if(playEnabled&&command.groundFollow&&command.interpolateFollow) {
        const auto pose=RenderMotionState(followCommand.followState,followNow.QuadPart,
            followFrequency.QuadPart,1000.0/60);
        target[0]=pose.x;target[2]=pose.z;
    }
    const bool groundFollow=playEnabled&&command.groundFollow;
    if(groundFollow) target[1]=sample->feet[2]; // Keep engine ground ownership.
    if(!(playEnabled?PilotFollowPosition(sample->feet,target,destination,continuous?32:2,continuous?2:.5):PilotPosition(sample->feet,target,destination))) {
        fail("native pilot: follow target exceeded finite distance bound; stopped");return;
    }
    const auto transform=reader.Pointer(sample->player+0xb0);
    // Match Teleport wrapper's existing-rotation argument without borrowing it
    // across a call. An unavailable transform disables this trial.
    if(!transform||!reader.Read(transform+0x20,rotation)) {fail("native pilot: current rotation unavailable; stopped");return;}
    for(float v:rotation) if(!std::isfinite(v)) {fail("native pilot: invalid rotation; stopped");return;}
    if(visibility==PilotVisibility::Acquire||visibility==PilotVisibility::Reapply) {
        const bool rehide=visibility==PilotVisibility::Reapply;
        reinterpret_cast<HideFn>(base+hideRva)(reinterpret_cast<void*>(sample->player),true,false,kPilotHideReason);
        hiddenPlayer=sample->player;status.hidden=true;
        if(!rehide||!lastRehideLog||now-lastRehideLog>=2000) {
            log(rehide?"native pilot: animation cleared owned hide reason; visibility restored to Minecraft":
                "native pilot: Geralt hidden with owned script reason");
            if(rehide) lastRehideLog=now;
        }
        if(!reader.Read(sample->player+0x30c,reasons)||!(reasons&kPilotHideReason)) {
            fail("native pilot: hide reason did not confirm visibility request; stopped");return;
        }
    }
    const double distance=std::hypot(destination[0]-sample->feet[0],destination[1]-sample->feet[1],destination[2]-sample->feet[2]);
    if(distance>0.05) {
        if(continuous&&!retryCadence.Due(now)) {PublishPilotStatus(status);return;}
        const bool moved=reinterpret_cast<TeleportFn>(base+teleportRva)(reinterpret_cast<void*>(sample->player),destination,rotation);
        if(continuous) retryCadence.Completed(moved,now);
        ++requests;
        if(requests<=3||(!moved&&now-lastRequestLog>=2000)) {
            float actual[3]{};
            const bool readable=reader.Read(sample->player+0xa0,actual);
            char line[256]{};std::snprintf(line,sizeof(line),"native pilot: teleport returned=%d request=(%.3f,%.3f,%.3f) immediateReadable=%d immediate=(%.3f,%.3f,%.3f)",moved,destination[0],destination[1],destination[2],readable,actual[0],actual[1],actual[2]);log(line);
            lastRequestLog=now;
        }
        if(!moved) {
            if(continuous) {PublishPilotStatus(status);return;}
            fail("native pilot: engine rejected teleport request; stopped");return;
        }
        // Water/animation controller motion can shift an accepted anchor step
        // by a few centimetres. Keep diagnostics exact; play retains a bounded
        // 10 cm acknowledgement plus the existing timeout and follow-lag limits.
        motion.Begin(destination,now,groundFollow,GroundSnapLimit(command),playEnabled?.1:.05);
    } else retryCadence.Reset();
    PublishPilotStatus(status);
}
}

