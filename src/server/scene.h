#pragma once

#include <cstdio>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

#include "physics_engine.h"
#include "server_game.h"
#include "shared/components.h"

enum class CollisionShape {
  Box,   // post-orientation AABB; cheap, works for procedural assets
  Mesh,  // triangle mesh; mesh-backed assets only
};

struct StaticEntityDesc {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  std::string modelName;  // "" → no visual entity
  glm::vec3 scale{1.0f, 1.0f, 1.0f};
  CollisionShape collision = CollisionShape::Box;
};

template <typename WorldTag>
void spawnStaticEntities(ServerGame& game,
                         const std::vector<StaticEntityDesc>& descs) {
  for (const auto& d : descs) {
    auto [id, entity] = new_entity(game);
    game.registry.template emplace<shared::Position>(
        entity, d.position.x, d.position.y, d.position.z, d.rotation.w,
        d.rotation.x, d.rotation.y, d.rotation.z);
    game.registry.template emplace<WorldTag>(entity);

    if (!d.modelName.empty()) {
      game.registry.template emplace<shared::RenderInfo>(
          entity, d.modelName, d.scale.x, d.scale.y, d.scale.z);
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

    JPH::BodyID bodyId =
        game.physics.createStaticBody(shape, d.position, d.rotation);
    game.registry.template emplace<shared::PhysicsBody>(
        entity, bodyId.GetIndexAndSequenceNumber());
  }
}
