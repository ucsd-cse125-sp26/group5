#include "server/game/puzzles/maze/trigger.h"

#include <cmath>

#include "server/game/puzzles/maze/layout_editor.h"
#include "server/game/puzzles/maze/puzzle.h"
#include "server/game/section_puzzle.h"
#include "server/server_game.h"
#include "shared/puzzles/maze/defaults.h"

namespace maze_trigger {

bool isInsideMazeTriggerRegion(const shared::Position& position,
                               const shared::maze_layout::Config& layout) {
  return layout.isInsideTrigger(position.x, position.y);
}

bool isInsideMazeTriggerRegion(const shared::Position& position,
                               const ServerGame& game) {
  return isInsideMazeTriggerRegion(position, game.mazeLayout);
}

bool allActivePlayersInMazeTrigger(const ServerGame& game) {
  if (game.active_players.size() <
      static_cast<size_t>(shared::maze_preview::kRequiredPlayersForStart)) {
    return false;
  }

  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      return false;
    }
    const auto& position =
        game.registry.get<shared::Position>(slots.overworld_avatar);
    if (!isInsideMazeTriggerRegion(position, game)) return false;
  }

  return true;
}

bool canTriggerMaze(const ServerGame& game) {
  if (maze_puzzle::isPuzzleActive(game)) return false;
  return section_puzzle::isSectionUnlocked(game,
                                           shared::SectionSeasonMap::WINTER);
}

glm::vec3 overworldSpawnPosition(const shared::maze_layout::Config& layout,
                                 uint8_t joinSlot) {
  const int idx =
      (joinSlot >= 1 && joinSlot <= 4) ? static_cast<int>(joinSlot) - 1 : 0;
  return {layout.spawnBaseX + layout.spawnOffsetX[idx],
          layout.spawnBaseY + layout.spawnOffsetY[idx], layout.spawnHeightZ};
}

glm::vec3 overworldSpawnPosition(const ServerGame& game, uint8_t joinSlot) {
  return overworldSpawnPosition(game.mazeLayout, joinSlot);
}

void placeOverworldAvatarInTrigger(ServerGame& game, entt::entity avatar,
                                   uint8_t joinSlot) {
  if (!game.registry.valid(avatar) ||
      !game.registry.all_of<shared::Position>(avatar)) {
    return;
  }
  const glm::vec3 spawn = overworldSpawnPosition(game, joinSlot);
  auto& pos = game.registry.get<shared::Position>(avatar);
  pos.x = spawn.x;
  pos.y = spawn.y;
  pos.z = spawn.z;
  if (game.registry.all_of<shared::Velocity>(avatar)) {
    auto& vel = game.registry.get<shared::Velocity>(avatar);
    vel.dx = vel.dy = vel.dz = 0.0f;
  }
}

std::vector<StaticEntityDesc> buildMazeTriggerMarkerEntities(
    const shared::maze_layout::Config& layout) {
  return maze_layout_editor::buildTriggerMarkerEntities(layout);
}

}  // namespace maze_trigger
