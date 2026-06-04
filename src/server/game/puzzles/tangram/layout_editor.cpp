#include "server/game/puzzles/tangram/layout_editor.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "server/server_game.h"
#include "shared/components.h"
#include "shared/puzzles/tangram/defaults.h"
#include "shared/puzzles/tangram/puzzle_data.h"

namespace tangram_layout_editor {
namespace {

std::vector<entt::entity> spawnDescBatch(
    ServerGame& game, const std::vector<StaticEntityDesc>& descs) {
  std::vector<entt::entity> created;
  created.reserve(descs.size());
  for (const StaticEntityDesc& d : descs) {
    auto [id, entity] = new_entity(game);
    (void)id;
    game.registry.emplace<shared::Position>(
        entity, d.position.x, d.position.y, d.position.z, d.rotation.w,
        d.rotation.x, d.rotation.y, d.rotation.z);
    game.registry.emplace<shared::OverworldTag>(entity);
    game.registry.emplace<shared::TangramLayoutVisual>(entity);
    if (!d.modelName.empty()) {
      game.registry.emplace<shared::RenderInfo>(entity, d.modelName, d.scale.x,
                                                d.scale.y, d.scale.z);
    }
    if (d.collision != CollisionShape::None) {
      JPH::ShapeRefC shape =
          game.physics.boxShapeForAsset(d.modelName, d.scale);
      if (shape) {
        JPH::BodyID bodyId = game.physics.createStaticBody(
            shape, d.position, d.rotation, d.staticFriction);
        game.registry.emplace<shared::PhysicsBody>(
            entity, bodyId.GetIndexAndSequenceNumber());
      }
    }
    created.push_back(entity);
  }
  return created;
}

}  // namespace

std::vector<StaticEntityDesc> buildArenaEntities(
    const shared::tangram::ArenaLayout& layout) {
  std::vector<StaticEntityDesc> entities;

  // Grey platform = full board + trigger pad (collision).
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(layout.platformCenterX, layout.platformCenterY,
                            layout.platformCenterZ),
      .modelName = "tangram_table",
      .scale = glm::vec3(layout.platformScaleX, layout.platformScaleY,
                         layout.platformScaleZ),
      .collision = CollisionShape::Box,
      .staticFriction = 1.05f,
  });

  const float topZ = layout.platformTopZ();
  const float cx = layout.boardCenterX;
  const float cy = layout.boardCenterY;
  const float goalHalfX = shared::tangram_puzzle::kShapeGoalHalfX;
  const float goalHalfY = shared::tangram_puzzle::kShapeGoalHalfY;
  constexpr float kSurface = 0.045f;
  constexpr float kRimT = 0.30f;
  constexpr float kRimH = 0.08f;

  // Black frame around the swan slot area only.
  const float rimZ = topZ + kSurface + kRimH * 0.5f;
  const float outerX = goalHalfX * 2.0f + kRimT;
  const float outerY = goalHalfY * 2.0f + kRimT;
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(cx, cy + goalHalfY + kRimT * 0.5f, rimZ),
      .modelName = "tangram_pad_black",
      .scale = glm::vec3(outerX, kRimT, kRimH),
      .collision = CollisionShape::None,
  });
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(cx, cy - goalHalfY - kRimT * 0.5f, rimZ),
      .modelName = "tangram_pad_black",
      .scale = glm::vec3(outerX, kRimT, kRimH),
      .collision = CollisionShape::None,
  });
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(cx + goalHalfX + kRimT * 0.5f, cy, rimZ),
      .modelName = "tangram_pad_black",
      .scale = glm::vec3(kRimT, outerY - kRimT * 2.0f, kRimH),
      .collision = CollisionShape::None,
  });
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(cx - goalHalfX - kRimT * 0.5f, cy, rimZ),
      .modelName = "tangram_pad_black",
      .scale = glm::vec3(kRimT, outerY - kRimT * 2.0f, kRimH),
      .collision = CollisionShape::None,
  });

  return entities;
}

std::vector<StaticEntityDesc> buildTriggerMarkerEntities(
    const shared::tangram::ArenaLayout& layout) {
  (void)layout;
  return {};
}

void spawnLayoutVisuals(ServerGame& game) {
  spawnDescBatch(game, buildArenaEntities(game.tangramArena));
}

}  // namespace tangram_layout_editor
