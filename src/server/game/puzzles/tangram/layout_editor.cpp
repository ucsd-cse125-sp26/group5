#include "server/game/puzzles/tangram/layout_editor.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "server/server_game.h"
#include "shared/components.h"
#include "shared/puzzles/tangram/defaults.h"

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
      auto& ri = game.registry.emplace<shared::RenderInfo>(
          entity, d.modelName, d.scale.x, d.scale.y, d.scale.z);
      ri.colorExempt = true;
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

  // One green play surface (trigger pad + board). Orange goal rim removed.
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(layout.platformCenterX, layout.platformCenterY,
                            layout.platformCenterZ),
      .modelName = "start_cube",
      .scale = glm::vec3(layout.platformScaleX, layout.platformScaleY,
                         layout.platformScaleZ),
      .collision = CollisionShape::Box,
      .staticFriction = 1.05f,
  });

  return entities;
}

std::vector<StaticEntityDesc> buildTriggerMarkerEntities(
    const shared::tangram::ArenaLayout& layout) {
  (void)layout;
  // Puzzle start uses spring_trigger Empty coords (see tangram_trigger.cpp),
  // not a visible/invisible marker cube here.
  return {};
}

void spawnLayoutVisuals(ServerGame& game) {
  spawnDescBatch(game, buildArenaEntities(game.tangramArena));
}

}  // namespace tangram_layout_editor
