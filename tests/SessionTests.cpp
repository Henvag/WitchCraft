#include "GameBridge.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using namespace witchcraft;
    using namespace skycraft::proto;
    Session session;
    GameFrame game;
    game.flags = kGamePlaying | kGameCollisionReady;
    game.worldId = 42;
    game.collisionEpoch = 9;
    game.feet[0] = 100; game.feet[1] = 20; game.feet[2] = -50;
    McState mc{};
    mc.flags = kMcInWorld;
    auto sky = session.Update(nullptr, &mc, true, 1920, 1080);
    Check(!sky.flags && !session.Control().active, "Absent adapter must not take control");
    game.flags=kGamePlaying|kGameScripted|kGameDiagnosticPatch;
    sky=session.Update(&game,&mc,true,1920,1080);
    Check(sky.flags==kSkyMenuOpen&&!session.Control().active,
        "Diagnostic patch flag must never enable normal control");
    game.flags=kGamePlaying|kGameCollisionReady;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    const auto firstTeleport = sky.teleportSeq;
    Check(firstTeleport && sky.posX == 100 && sky.posY == 20 && sky.posZ == -50,
        "Initial destination must be Witcher's feet");
    Check(sky.flags == kSkyInGame && !session.Control().active,
        "Publish spawn to MC but await its acknowledgement");
    mc.teleportAck = firstTeleport - 1;
    for (int i = 0; i < 10; ++i) {
        sky = session.Update(&game, &mc, true, 1920, 1080);
        Check(sky.teleportSeq == firstTeleport && !session.Control().active,
            "Collision wait acknowledgement must never be copied into teleportSeq");
    }
    mc.teleportAck = firstTeleport;
    session.Update(&game, &mc, true, 1920, 1080);
    Check(!session.Control().active, "Stale acknowledgement at wrong destination must be ignored");
    mc.x = 100; mc.y = 20; mc.z = -50;
    session.Update(&game, &mc, true, 1920, 1080);
    Check(session.Control().active, "Matching position and acknowledgement enables locomotion");
    mc.x += 8; mc.y += 3; mc.flags |= kMcFlying;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(session.Control().feet[0] == 108 && session.Control().feet[1] == 23 &&
        (session.Control().mcFlags & kMcFlying), "MC owns position and movement flags after sync");
    Check(sky.teleportSeq == firstTeleport, "Ordinary movement must not teleport MC every frame");
    game.flags |= kGameScripted;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(sky.flags == kSkyMenuOpen && !session.Control().active, "Scenes hand control to Witcher");
    game.flags &= ~kGameScripted;
    game.feet[0] = 500;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(sky.teleportSeq != firstTeleport && sky.posX == 500 && !session.Control().active,
        "Returning from a scene must re-anchor at the new Witcher position");
    auto seq = sky.teleportSeq;
    ++game.relocation;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(sky.teleportSeq != seq, "External teleports require a new acknowledgement");
    seq = sky.teleportSeq;
    ++game.worldId; ++game.collisionEpoch;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(sky.teleportSeq != seq && sky.collisionEpoch == game.collisionEpoch,
        "World changes refresh collision and teleport");
    seq = sky.teleportSeq;
    session.Update(&game, &mc, false, 1920, 1080);
    Check(!session.Control().active, "Minecraft disconnect restores Witcher control");
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(sky.teleportSeq != seq, "Reconnect requires a fresh handshake");
    mc.teleportAck = sky.teleportSeq;
    mc.x = game.feet[0]; mc.y = game.feet[1]; mc.z = game.feet[2];
    mc.flags |= kMcDead;
    session.Update(&game, &mc, true, 1920, 1080);
    Check(!session.Control().active, "Dead MC must not drive Geralt");
    mc.flags &= ~kMcDead;
    mc.x = std::numeric_limits<double>::quiet_NaN();
    session.Update(&game, &mc, true, 1920, 1080);
    Check(!session.Control().active, "Nonfinite coordinates must not reach REDengine");
    game.flags &= ~kGameCollisionReady;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(!sky.flags && !session.Control().active, "No movement without real collision");
    game.flags |= kGameLoading;
    sky = session.Update(&game, &mc, true, 1920, 1080);
    Check(sky.flags == kSkyLoading, "Loading must reach Minecraft");
    game.flags = kGamePlaying | kGameCollisionReady;
    game.forward[0] = -1; game.forward[2] = 0;
    sky = session.Update(&game, &mc, true, 1280, 720);
    Check(std::abs(sky.yaw - 90) < 0.01 && sky.viewportW == 1280,
        "Look uses engine camera in MC convention");
    std::cout << "Session synchronization, handoff, reconnect, and invalid-data checks passed\n";
}
