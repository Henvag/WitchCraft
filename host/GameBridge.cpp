#include "GameBridge.h"
#include "Link.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace {
std::mutex mutex;
witchcraft::GameFrame latest{};
witchcraft::GameControl control{};
witchcraft::DiagnosticWalls diagnosticWalls;
witchcraft::OcclusionGeometry occlusionGeometry;
witchcraft::PilotCommand pilotCommand;
witchcraft::PilotStatus pilotStatus;
ULONGLONG pilotTime=0;
ULONGLONG frameTime = 0, controlTime = 0;
constexpr ULONGLONG timeout = 250;
struct Collision { std::uint32_t type; std::vector<std::uint8_t> data; };
std::deque<Collision> pending;
std::size_t pendingBytes = 0;
std::uint32_t deliveredEpoch = 0;
bool haveClear = false;
constexpr std::size_t maxPending = 8 * 1024 * 1024;
}

extern "C" int WitchCraftSubmitFrame(const witchcraft::GameFrame* frame)
{
    if (!frame || frame->size != sizeof(*frame) || frame->version != witchcraft::kBridgeVersion)
        return 0;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(frame->feet[i]) || !std::isfinite(frame->camera[i]) ||
            !std::isfinite(frame->forward[i])) return 0;
    const auto length = std::hypot(frame->forward[0], frame->forward[1], frame->forward[2]);
    if (length < 0.9 || length > 1.1 || !std::isfinite(frame->verticalFov) ||
        frame->verticalFov <= 20 || frame->verticalFov >= 140 ||
        !std::isfinite(frame->gameHour)) return 0;
    std::lock_guard lock(mutex);
    latest = *frame;
    frameTime = ::GetTickCount64();
    return 1;
}

extern "C" int WitchCraftReadControl(witchcraft::GameControl* out)
{
    if (!out || out->size != sizeof(*out) || out->version != witchcraft::kBridgeVersion) return 0;
    std::lock_guard lock(mutex);
    *out = control;
    const auto now = ::GetTickCount64();
    if (!controlTime || now - controlTime > timeout || !frameTime || now - frameTime > timeout)
        out->active = 0;
    return 1;
}

extern "C" int WitchCraftSubmitCollision(std::uint32_t type, const void* data, std::uint32_t bytes)
{
    using namespace skycraft::proto;
    if (!data) return 0;
    if (type == kColClear) {
        if (bytes != sizeof(std::uint32_t)) return 0;
    } else if (type == kColRegion || type == kColTris) {
        if (bytes < sizeof(ColRegion)) return 0;
        ColRegion region{};
        std::memcpy(&region, data, sizeof(region));
        const std::uint64_t stride = type == kColTris ? sizeof(ColTri) : sizeof(ColBlock);
        if (sizeof(region) + stride * region.count != bytes ||
            region.minX > region.maxX || region.minY > region.maxY || region.minZ > region.maxZ) return 0;
    } else return 0;
    std::lock_guard lock(mutex);
    if (bytes > maxPending || pendingBytes + bytes > maxPending) return 0;
    // Keep clear messages ordered with their regions. The caller retries on 0.
    Collision message{type, std::vector<std::uint8_t>(bytes)};
    std::memcpy(message.data.data(), data, bytes);
    pending.push_back(std::move(message));
    pendingBytes += bytes;
    return 1;
}

namespace witchcraft {
void PublishPilotCommand(const PilotCommand& value) {
    std::lock_guard lock(mutex); pilotCommand=value; pilotTime=::GetTickCount64();
    for(double v:value.feet) if(!std::isfinite(v)) pilotCommand.active=false;
}
PilotCommand ReadPilotCommand() {
    std::lock_guard lock(mutex); auto result=pilotCommand;
    if(!pilotTime||::GetTickCount64()-pilotTime>250) result.active=false;
    return result;
}
void PublishPilotStatus(const PilotStatus& value) { std::lock_guard lock(mutex);pilotStatus=value; }
PilotStatus ReadPilotStatus() { std::lock_guard lock(mutex);return pilotStatus; }
void PublishDiagnosticWalls(const DiagnosticWalls& walls) {
    std::lock_guard lock(mutex); diagnosticWalls=walls;
}
bool ReadDiagnosticWalls(const GameFrame& frame,DiagnosticWalls& walls) {
    std::lock_guard lock(mutex);
    if(!(frame.flags&kGameDiagnosticPatch)||frame.worldId!=diagnosticWalls.world||
        frame.collisionEpoch!=diagnosticWalls.epoch||frame.relocation!=diagnosticWalls.relocation) return false;
    walls=diagnosticWalls; return true;
}
bool ReadGameFrame(GameFrame& out)
{
    std::lock_guard lock(mutex);
    if (!frameTime || ::GetTickCount64() - frameTime > timeout) return false;
    out = latest;
    return true;
}
void PublishOcclusionGeometry(const DiagnosticWalls& geometry) {
    std::lock_guard lock(mutex);
    static_cast<DiagnosticWalls&>(occlusionGeometry)=geometry;
    if(++occlusionGeometry.revision==0) ++occlusionGeometry.revision;
}
bool ReadOcclusionGeometry(const GameFrame& frame,std::uint64_t knownRevision,OcclusionGeometry& out) {
    std::lock_guard lock(mutex);
    if(!occlusionGeometry.revision||frame.worldId!=occlusionGeometry.world||
        frame.collisionEpoch!=occlusionGeometry.epoch||frame.relocation!=occlusionGeometry.relocation) return false;
    if(knownRevision!=occlusionGeometry.revision) out=occlusionGeometry;
    return true;
}
void PublishGameControl(const GameControl& value)
{
    std::lock_guard lock(mutex);
    control = value;
    controlTime = ::GetTickCount64();
}
void PumpGameCollision()
{
    std::lock_guard lock(mutex);
    auto& link = Link::Get();
    while (!pending.empty()) {
        const auto& msg = pending.front();
        if (!link.WriteCollision(static_cast<skycraft::proto::ColType>(msg.type),
            msg.data.data(), static_cast<std::uint32_t>(msg.data.size()))) break;
        if (msg.type == skycraft::proto::kColClear) {
            std::memcpy(&deliveredEpoch, msg.data.data(), sizeof(deliveredEpoch));
            haveClear = true;
        }
        pendingBytes -= msg.data.size();
        pending.pop_front();
    }
}
bool GameCollisionDrained(std::uint32_t epoch)
{
    std::lock_guard lock(mutex);
    return haveClear && deliveredEpoch == epoch && pending.empty();
}
}
