#pragma once

#include <glm/vec3.hpp>
#include <vector>

#include "server/scene.h"
#include "shared/components.h"
#include "shared/maze_preview.h"

struct ServerGame;

namespace maze_trigger {

// Horizontal AABB in x,y where players stand before the maze (z ignored).
constexpr float kCenterX = shared::maze_preview::kTriggerCenterX;
constexpr float kCenterY = shared::maze_preview::kTriggerCenterY;
constexpr float kHalfExtent = shared::maze_preview::kHalfExtent;

[[nodiscard]] bool isInsideMazeTriggerRegion(const shared::Position& position);

// True only when four clients are connected and every overworld avatar is in
// the trigger AABB.
[[nodiscard]] bool allActivePlayersInMazeTrigger(const ServerGame& game);

[[nodiscard]] std::vector<StaticEntityDesc> buildMazeTriggerMarkerEntities();

// Spawn / reconnect position on the trigger pad for join slot 1–4.
[[nodiscard]] glm::vec3 overworldSpawnPosition(uint8_t joinSlot);

// Teleport overworld avatar onto the pad and face the maze board.
void placeOverworldAvatarInTrigger(ServerGame& game, entt::entity avatar,
                                   uint8_t joinSlot);

}  // namespace maze_trigger
