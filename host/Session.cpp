#include "GameBridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace witchcraft {
skycraft::proto::SkyState Session::Update(const GameFrame* game,
    const skycraft::proto::McState* mc, bool mcAlive,
    std::uint32_t width, std::uint32_t height)
{
    using namespace skycraft::proto;
    control_ = {};
    if (mcAlive && !wasMcAlive_) initialized_ = false;
    wasMcAlive_ = mcAlive;
    SkyState sky{};
    sky.viewportW = width;
    sky.viewportH = height;
    sky.teleportSeq = teleport_;
    if (!game) {
        initialized_ = false;
        return sky;
    }
    sky.worldId = game->worldId;
    sky.collisionEpoch = game->collisionEpoch;
    sky.gameHour = game->gameHour;
    constexpr float degrees = 57.295779513f;
    sky.yaw = std::atan2(-game->forward[0], game->forward[2]) * degrees;
    sky.pitch = std::atan2(-game->forward[1],
        std::hypot(game->forward[0], game->forward[2])) * degrees;
    const bool playable = (game->flags & kGamePlaying) &&
        !(game->flags & (kGameMenu | kGameLoading | kGameScripted));
    if (!playable) {
        sky.flags = (game->flags & kGameLoading) ? kSkyLoading : kSkyMenuOpen;
        // Re-anchor on return from a scene, menu or loading screen. Never apply
        // the last Minecraft position after Witcher has taken control.
        initialized_ = false;
        return sky;
    }
    if (!initialized_ || world_ != game->worldId || relocation_ != game->relocation ||
        epoch_ != game->collisionEpoch) {
        initialized_ = true;
        world_ = game->worldId;
        relocation_ = game->relocation;
        epoch_ = game->collisionEpoch;
        std::copy_n(game->feet, 3, destination_);
        if (++teleport_ == 0) ++teleport_;
        awaiting_ = true;
    }
    sky.teleportSeq = teleport_;
    sky.posX = destination_[0];
    sky.posY = destination_[1];
    sky.posZ = destination_[2];
    const bool finite = mc && std::isfinite(mc->x) && std::isfinite(mc->y) &&
        std::isfinite(mc->z) && std::isfinite(mc->yaw) && std::isfinite(mc->pitch);
    if (game->flags & kGameCollisionReady) sky.flags = kSkyInGame;
    const bool ready = mcAlive && finite && (mc->flags & kMcInWorld) &&
        (game->flags & kGameCollisionReady);
    if (!ready) return sky;
    if (mc->teleportAck != teleport_) return sky;
    if (awaiting_) {
        // Ignore an old acknowledgement with an old position after reconnect.
        const double dx = mc->x - destination_[0], dy = mc->y - destination_[1],
            dz = mc->z - destination_[2];
        if (dx * dx + dy * dy + dz * dz > 4.0) return sky;
        awaiting_ = false;
    }
    if (mc->flags & kMcDead) return sky;
    control_.active = 1;
    control_.mcFlags = mc->flags;
    control_.feet[0] = mc->x;
    control_.feet[1] = mc->y;
    control_.feet[2] = mc->z;
    control_.yaw = mc->yaw;
    control_.pitch = mc->pitch;
    return sky;
}
}
