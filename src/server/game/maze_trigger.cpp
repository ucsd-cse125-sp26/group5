#include "server/game/maze_trigger.h"

#include <cmath>

#include "server/game/maze_camera.h"
#include "server/server_game.h"

namespace maze_trigger {

bool isInsideMazeTriggerRegion(const shared::Position& position) {
  return std::abs(position.x - kCenterX) <= kHalfExtent &&
         std::abs(position.y - kCenterY) <= kHalfExtent;
}

bool allActivePlayersInMazeTrigger(const ServerGame& game) {
  if (game.active_players.size() != 4) return false;

  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      return false;
    }
    const auto& position =
        game.registry.get<shared::Position>(slots.overworld_avatar);
    if (!isInsideMazeTriggerRegion(position)) return false;
  }

  return true;
}

glm::vec3 overworldSpawnPosition(uint8_t joinSlot) {
  static constexpr float kSpawns[4][2] = {{-2.0f, 9.0f}, {2.0f, 9.0f},
                                          {-2.0f, 11.0f}, {2.0f, 11.0f}};
  const int idx =
      (joinSlot >= 1 && joinSlot <= 4) ? static_cast<int>(joinSlot) - 1 : 0;
  return glm::vec3(kSpawns[idx][0], kSpawns[idx][1], 0.5f);
}

void placeOverworldAvatarInTrigger(ServerGame& game, entt::entity avatar,
                                   uint8_t joinSlot) {
  if (!game.registry.valid(avatar) ||
      !game.registry.all_of<shared::Position>(avatar)) {
    return;
  }
  const glm::vec3 spawn = overworldSpawnPosition(joinSlot);
  auto& pos = game.registry.get<shared::Position>(avatar);
  pos.x = spawn.x;
  pos.y = spawn.y;
  pos.z = spawn.z;
  if (game.registry.all_of<shared::Velocity>(avatar)) {
    auto& vel = game.registry.get<shared::Velocity>(avatar);
    vel.dx = vel.dy = vel.dz = 0.0f;
  }
  maze_camera::snapOverworldAvatarFaceMazePreview(game, avatar);
}

std::vector<StaticEntityDesc> buildMazeTriggerMarkerEntities() {
  std::vector<StaticEntityDesc> entities;

  const float minX = kCenterX - kHalfExtent;
  const float maxX = kCenterX + kHalfExtent;
  const float minY = kCenterY - kHalfExtent;
  const float maxY = kCenterY + kHalfExtent;
  constexpr float kBorderZ = 0.08f;
  constexpr glm::vec3 kBorderScale(0.45f, 0.45f, 0.16f);
  constexpr glm::vec3 kCornerScale(0.55f, 0.55f, 1.6f);

  for (const glm::vec3& pos :
       {glm::vec3(minX, minY, 0.8f), glm::vec3(minX, maxY, 0.8f),
        glm::vec3(maxX, minY, 0.8f), glm::vec3(maxX, maxY, 0.8f)}) {
    entities.push_back(StaticEntityDesc{
        .position = pos,
        .modelName = "goal_cube",
        .scale = kCornerScale,
    });
  }

  for (float x = minX + 1.0f; x < maxX; x += 2.0f) {
    entities.push_back(StaticEntityDesc{
        .position = glm::vec3(x, minY, kBorderZ),
        .modelName = "start_cube",
        .scale = kBorderScale,
    });
    entities.push_back(StaticEntityDesc{
        .position = glm::vec3(x, maxY, kBorderZ),
        .modelName = "start_cube",
        .scale = kBorderScale,
    });
  }

  for (float y = minY + 1.0f; y < maxY; y += 2.0f) {
    entities.push_back(StaticEntityDesc{
        .position = glm::vec3(minX, y, kBorderZ),
        .modelName = "start_cube",
        .scale = kBorderScale,
    });
    entities.push_back(StaticEntityDesc{
        .position = glm::vec3(maxX, y, kBorderZ),
        .modelName = "start_cube",
        .scale = kBorderScale,
    });
  }

  return entities;
}

}  // namespace maze_trigger
