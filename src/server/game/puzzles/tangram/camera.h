#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace tangram_camera {

constexpr float kFocusHoldSeconds = 1.0f;

void snapOverworldAvatarFaceTangramBoard(ServerGame& game, entt::entity avatar);
void snapOverworldAvatarsFaceTangramBoard(ServerGame& game);

[[nodiscard]] bool allOverworldAvatarsFacingTangramBoard(
    const ServerGame& game, float maxYawErrorRad = 0.35f);

}  // namespace tangram_camera
