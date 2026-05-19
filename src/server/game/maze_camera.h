#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace maze_camera {

// Seconds all four players must stand in the trigger (facing the preview)
// before auto-entering the maze.
constexpr float kFocusHoldSeconds = 1.0f;

// Snap overworld avatar yaw so +Y faces the maze preview center.
void snapOverworldAvatarFaceMazePreview(ServerGame& game, entt::entity avatar);
void snapOverworldAvatarsFaceMazePreview(ServerGame& game);

[[nodiscard]] bool allOverworldAvatarsFacingMazePreview(
    const ServerGame& game, float maxYawErrorRad = 0.35f);

}  // namespace maze_camera
