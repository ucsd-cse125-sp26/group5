#pragma once

#include <glm/vec3.hpp>
#include <vector>

#include "server/scene.h"
#include "shared/components.h"
#include "shared/puzzles/maze/layout.h"

struct ServerGame;

namespace maze_trigger {

[[nodiscard]] bool isInsideMazeTriggerRegion(
    const shared::Position& position, const shared::maze_layout::Config& layout);

[[nodiscard]] bool isInsideMazeTriggerRegion(const shared::Position& position,
                                           const ServerGame& game);

[[nodiscard]] bool allActivePlayersInMazeTrigger(const ServerGame& game);

// Winter preview maze: only while section is unlocked and not yet completed.
[[nodiscard]] bool canTriggerMaze(const ServerGame& game);

[[nodiscard]] std::vector<StaticEntityDesc> buildMazeTriggerMarkerEntities(
    const shared::maze_layout::Config& layout);

[[nodiscard]] glm::vec3 overworldSpawnPosition(
    const shared::maze_layout::Config& layout, uint8_t joinSlot);

[[nodiscard]] glm::vec3 overworldSpawnPosition(const ServerGame& game,
                                               uint8_t joinSlot);

void placeOverworldAvatarInTrigger(ServerGame& game, entt::entity avatar,
                                   uint8_t joinSlot);

}  // namespace maze_trigger
