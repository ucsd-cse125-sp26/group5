#include "scene.h"

#include <cstdio>

#include "physics_engine.h"
#include "server_game.h"
#include "shared/components.h"

void spawnStaticEntities(ServerGame& game,
                         const std::vector<StaticEntityDesc>& descs) {
  for (const auto& d : descs) {
    auto [id, entity] = new_entity(game);
    game.registry.emplace<shared::Position>(
        entity, d.position.x, d.position.y, d.position.z, d.rotation.w,
        d.rotation.x, d.rotation.y, d.rotation.z);

    if (!d.modelName.empty()) {
      game.registry.emplace<shared::RenderInfo>(entity, d.modelName, d.scale.x,
                                                d.scale.y, d.scale.z);
    }

    JPH::ShapeRefC shape;
    if (d.collision == CollisionShape::Mesh) {
      shape = game.physics.meshShapeForAsset(d.modelName, d.scale);
      if (!shape) {
        printf(
            "spawnStaticEntities: %s has no mesh geometry, falling back "
            "to box\n",
            d.modelName.c_str());
        shape = game.physics.boxShapeForAsset(d.modelName, d.scale);
      }
    } else {
      shape = game.physics.boxShapeForAsset(d.modelName, d.scale);
    }
    if (!shape) continue;

    // Asset orientation is baked into the shape, so the body sits at
    // (d.position, d.rotation) and the per-tick Jolt→ECS sync is a no-op.
    JPH::BodyID bodyId =
        game.physics.createStaticBody(shape, d.position, d.rotation);
    game.registry.emplace<shared::PhysicsBody>(
        entity, bodyId.GetIndexAndSequenceNumber());
  }
}
