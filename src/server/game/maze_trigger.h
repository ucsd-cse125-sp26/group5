#pragma once

#include <vector>

#include "server/scene.h"
#include "shared/components.h"
#include "shared/maze_preview.h"

struct ServerGame;

namespace maze_trigger {

// Horizontal AABB in x,y around the overworld maze preview (z ignored).
constexpr float kCenterX = shared::maze_preview::kCenterX;
constexpr float kCenterY = shared::maze_preview::kCenterY;
constexpr float kHalfExtent = shared::maze_preview::kHalfExtent;

[[nodiscard]] bool isInsideMazeTriggerRegion(const shared::Position& position);

// True only when four clients are connected and every overworld avatar is in
// the trigger AABB.
[[nodiscard]] bool allActivePlayersInMazeTrigger(const ServerGame& game);

[[nodiscard]] std::vector<StaticEntityDesc> buildMazeTriggerMarkerEntities();

}  // namespace maze_trigger
