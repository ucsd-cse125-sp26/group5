#pragma once

#include <vector>

#include "server/scene.h"
#include "shared/components.h"

struct ServerGame;

namespace maze_trigger {

// Horizontal AABB in x,y around the overworld maze preview (z ignored).
constexpr float kCenterX = 0.0f;
constexpr float kCenterY = 16.0f;
constexpr float kHalfExtent = 4.0f;

[[nodiscard]] bool isInsideMazeTriggerRegion(const shared::Position& position);

// True only when four clients are connected and every overworld avatar is in
// the trigger AABB.
[[nodiscard]] bool allActivePlayersInMazeTrigger(const ServerGame& game);

[[nodiscard]] std::vector<StaticEntityDesc> buildMazeTriggerMarkerEntities();

}  // namespace maze_trigger
