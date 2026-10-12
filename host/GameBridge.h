#pragma once

#include "skycraft_protocol.h"
#include <cstdint>
#include <vector>

// Versioned adapter boundary. All coordinates are Minecraft space: metres,
// Y up. The REDengine adapter converts its own coordinates at this boundary.
// Only the adapter may touch REDengine objects, on the game's update thread.
namespace witchcraft
{
inline constexpr std::uint32_t kBridgeVersion = 1;
enum GameFlags : std::uint32_t {
    kGamePlaying = 1, kGameMenu = 2, kGameLoading = 4,
    kGameScripted = 8, kGameCollisionReady = 16,
    kGameDiagnosticPatch = 32, // Sampled patch only; never enables normal control.
    kGamePlayPatch = 64 // Published 16 m patch with a 2 m streaming margin.
};
struct GameFrame {
    std::uint32_t size = sizeof(GameFrame), version = kBridgeVersion;
    std::uint32_t flags = 0, worldId = 0;
    // Increment on loads, doors, fast travel, and every external teleport.
    std::uint32_t relocation = 0, collisionEpoch = 0;
    double feet[3]{};
    double camera[3]{};
    float forward[3]{0, 0, 1};
    float verticalFov = 70, gameHour = 12;
};
struct GameControl {
    std::uint32_t size = sizeof(GameControl), version = kBridgeVersion;
    // When zero, restore Geralt's previous visibility and normal locomotion.
    std::uint32_t active = 0, mcFlags = 0;
    double feet[3]{};
    float yaw = 0, pitch = 0;
};
static_assert(sizeof(GameFrame) == 96);
static_assert(sizeof(GameControl) == 48);

// Pure synchronization logic, also exercised without either game running.
class Session {
public:
    skycraft::proto::SkyState Update(const GameFrame* game,
        const skycraft::proto::McState* mc, bool mcAlive,
        std::uint32_t width, std::uint32_t height);
    const GameControl& Control() const { return control_; }
private:
    bool initialized_ = false, awaiting_ = false, wasMcAlive_ = false;
    std::uint32_t world_ = 0, relocation_ = 0, epoch_ = 0, teleport_ = 0;
    double destination_[3]{};
    GameControl control_{};
};

bool ReadGameFrame(GameFrame& out);
struct PilotCommand {
    bool active=false;
    bool cameraOwned=false;
    std::uint64_t publishedAt=0;
    bool groundFollow=false; // Native anchor stays on terrain during MC jumps.
    bool interpolateFollow=false;
    skycraft::proto::McState followState{}; // Tick snapshot, evaluated on the game thread.
    std::uint32_t world=0,epoch=0,relocation=0;
    double feet[3]{};
};
struct PilotStatus { bool hidden=false,failed=false; std::uint32_t moves=0; };
void PublishPilotCommand(const PilotCommand& command);
PilotCommand ReadPilotCommand();
void PublishPilotStatus(const PilotStatus& status);
PilotStatus ReadPilotStatus();
struct DiagnosticWalls {
    std::uint32_t world=0,epoch=0,relocation=0;
    std::vector<skycraft::proto::ColTri> triangles;
};
void PublishDiagnosticWalls(const DiagnosticWalls& walls);
bool ReadDiagnosticWalls(const GameFrame& frame,DiagnosticWalls& walls);
bool GameCollisionDrained(std::uint32_t epoch);
void PublishGameControl(const GameControl& control);
void PumpGameCollision();
}

// Resolve these exports from dinput8.dll with GetProcAddress. No object pointers
// cross threads. Calls return 0 on invalid version/data/backpressure.
extern "C" {
int WitchCraftSubmitFrame(const witchcraft::GameFrame* frame);
int WitchCraftReadControl(witchcraft::GameControl* control);
int WitchCraftSubmitCollision(std::uint32_t type, const void* payload, std::uint32_t bytes);
}
