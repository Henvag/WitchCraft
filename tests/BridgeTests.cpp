#include "GameBridge.h"
#include "Link.h"
#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {
bool accept = true;
std::vector<std::uint32_t> delivered;
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
}
// Test transport without opening the real SkyCraft shared-memory mapping.
namespace witchcraft {
Link& Link::Get() { static Link link; return link; }
bool Link::WriteCollision(skycraft::proto::ColType type, const void*, std::uint32_t) {
    if (!accept) return false;
    delivered.push_back(type);
    return true;
}
}
int main() {
    using namespace witchcraft;
    using namespace skycraft::proto;
    GameFrame frame, copy;
    Check(!ReadGameFrame(copy), "No frame before submission");
    frame.version = 999;
    Check(!WitchCraftSubmitFrame(&frame), "Reject ABI mismatch");
    frame.version = kBridgeVersion;
    frame.camera[0] = std::numeric_limits<double>::infinity();
    Check(!WitchCraftSubmitFrame(&frame), "Reject infinite camera");
    frame.camera[0] = 0;
    frame.forward[2] = 0;
    Check(!WitchCraftSubmitFrame(&frame), "Reject invalid camera basis");
    frame.forward[2] = 1;
    Check(WitchCraftSubmitFrame(&frame) && ReadGameFrame(copy), "Valid frame crosses bridge");
    GameControl control;
    control.active = 1; control.feet[0] = 123;
    PublishGameControl(control);
    Check(WitchCraftReadControl(&control) && control.active && control.feet[0] == 123,
        "Adapter receives copied motion control");
    control.size = 0;
    Check(!WitchCraftReadControl(&control), "Reject invalid output size");
    Check(!WitchCraftSubmitCollision(kColClear, nullptr, 4), "Reject null collision");
    std::uint32_t epoch = 12;
    Check(!WitchCraftSubmitCollision(kColClear, &epoch, 3), "Reject truncated clear");
    Check(!GameCollisionDrained(epoch), "Collision cannot be ready before clear");
    Check(WitchCraftSubmitCollision(kColClear, &epoch, sizeof(epoch)), "Queue clear");
    ColRegion region{};
    region.epoch = epoch;
    region.count = 1;
    Check(!WitchCraftSubmitCollision(kColTris, &region, sizeof(region)), "Reject truncated triangles");
    region.count = 0;
    Check(WitchCraftSubmitCollision(kColTris, &region, sizeof(region)), "Queue empty region");
    accept = false;
    PumpGameCollision();
    Check(delivered.empty() && !GameCollisionDrained(epoch), "Backpressure preserves pending data");
    accept = true;
    PumpGameCollision();
    Check(delivered.size() == 2 && delivered[0] == kColClear && delivered[1] == kColTris &&
        GameCollisionDrained(epoch) && !GameCollisionDrained(epoch + 1), "Preserve order and epoch readiness");
    DiagnosticWalls walls{7,12,3,{ColTri{}}}, wallCopy;
    frame.flags=kGameDiagnosticPatch; frame.worldId=7; frame.collisionEpoch=12; frame.relocation=3;
    PublishDiagnosticWalls(walls); walls.triangles.clear();
    Check(ReadDiagnosticWalls(frame,wallCopy)&&wallCopy.triangles.size()==1,"Wall mesh copied across threads");
    ++frame.relocation;
    Check(!ReadDiagnosticWalls(frame,wallCopy),"Reject wall mesh after relocation");
    --frame.relocation; ++frame.collisionEpoch;
    Check(!ReadDiagnosticWalls(frame,wallCopy),"Reject wall mesh after epoch change");
    --frame.collisionEpoch; frame.flags=0;
    Check(!ReadDiagnosticWalls(frame,wallCopy),"Wall mesh requires current diagnostic patch");
    DiagnosticWalls geometry{frame.worldId,frame.collisionEpoch,frame.relocation,{ColTri{}}};
    PublishOcclusionGeometry(geometry);OcclusionGeometry depth;
    Check(ReadOcclusionGeometry(frame,0,depth)&&depth.revision&&depth.triangles.size()==1,"Current occlusion copied");
    const auto revision=depth.revision;OcclusionGeometry unchanged;
    Check(ReadOcclusionGeometry(frame,revision,unchanged)&&unchanged.triangles.empty(),"Unchanged depth avoids mesh copies");
    ++frame.worldId;Check(!ReadOcclusionGeometry(frame,revision,unchanged),"Old world depth rejected");--frame.worldId;
    ++frame.collisionEpoch;Check(!ReadOcclusionGeometry(frame,revision,unchanged),"Old epoch depth rejected");--frame.collisionEpoch;
    ++frame.relocation;Check(!ReadOcclusionGeometry(frame,revision,unchanged),"Old location depth rejected");--frame.relocation;
    geometry.triangles.clear();PublishOcclusionGeometry(geometry);
    Check(ReadOcclusionGeometry(frame,revision,depth)&&depth.revision!=revision&&depth.triangles.empty(),"Empty replacement clears depth");
    ::Sleep(270);
    control = {};
    Check(!ReadGameFrame(copy) && WitchCraftReadControl(&control) && !control.active,
        "Adapter timeout restores control even if host stalls");
    PilotCommand command; command.active=true;command.feet[0]=1;
    PublishPilotCommand(command);Check(ReadPilotCommand().active,"Pilot receives fresh copied command");
    command.feet[0]=std::numeric_limits<double>::quiet_NaN();PublishPilotCommand(command);
    Check(!ReadPilotCommand().active,"Invalid pilot position disables native action");
    command.feet[0]=1;PublishPilotCommand(command);::Sleep(270);
    Check(!ReadPilotCommand().active,"Expired pilot command disables native action request");
    std::cout << "Bridge ABI, validation, queue order, backpressure, and timeout checks passed\n";
}
