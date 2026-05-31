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
  None,  // visual-only marker
  Box,   // post-orientation AABB; cheap, works for procedural assets
  Mesh,  // triangle mesh; mesh-backed assets only
};

struct StaticEntityDesc {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  std::string modelName;  // "" → no visual entity
  glm::vec3 scale{1.0f, 1.0f, 1.0f};
  CollisionShape collision = CollisionShape::Box;
  /// If >= 0, applied to static Jolt body (higher = grippier tangram platform).
  float staticFriction = -1.0f;
  std::vector<shared::SoundLayer> soundLayers;
};

template <typename WorldTag>
entt::entity spawnSectionBarrier(ServerGame& game, uint8_t sectionID,
                                 shared::SectionSeasonMap season,
                                 const glm::vec3& pos,
                                 const glm::vec3& halfExtents) {
  auto [id, entity] = new_entity(game);
  glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};

  game.registry.template emplace<shared::Position>(entity, pos.x, pos.y, pos.z,
                                                   rot.w, rot.x, rot.y, rot.z);
  game.registry.template emplace<WorldTag>(entity);
  game.registry.template emplace<shared::SectionBarrierTag>(
      entity, sectionID, halfExtents, season);
  // renderInfo makes the barriers visible
  // game.registry.template emplace<shared::RenderInfo>(
  //     entity, "cube",
  //     halfExtents.x * 2.0f,
  //     halfExtents.y * 2.0f,
  //     halfExtents.z * 2.0f);

  JPH::BodyID bodyId = game.physics.createBoxBody(halfExtents, pos, rot);
  game.registry.template emplace<shared::PhysicsBody>(
      entity, bodyId.GetIndexAndSequenceNumber());

  return entity;
}

template <typename WorldTag>
void spawnInvisibleWall(ServerGame& game, const glm::vec3& pos,
                        const glm::vec3& halfExtents) {
  auto [id, entity] = new_entity(game);
  glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
  game.registry.template emplace<shared::Position>(entity, pos.x, pos.y, pos.z,
                                                   rot.w, rot.x, rot.y, rot.z);
  game.registry.template emplace<WorldTag>(entity);
  // renderInfo makes the barriers visible
  // game.registry.template emplace<shared::RenderInfo>(
  //     entity, "cube",
  //     halfExtents.x * 2.0f,
  //     halfExtents.y * 2.0f,
  //     halfExtents.z * 2.0f);
  JPH::BodyID bodyId = game.physics.createBoxBody(halfExtents, pos, rot);
  game.registry.template emplace<shared::PhysicsBody>(
      entity, bodyId.GetIndexAndSequenceNumber());
}

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

    if (d.collision == CollisionShape::None) continue;

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
        game.physics.createStaticBody(shape, d.position, d.rotation,
                                      d.staticFriction);
    game.registry.template emplace<shared::PhysicsBody>(
        entity, bodyId.GetIndexAndSequenceNumber());
    if (!d.soundLayers.empty()) {
      shared::SoundEmitter emitter;
      emitter.layerCount = static_cast<uint8_t>(
          std::min(d.soundLayers.size(),
                   static_cast<size_t>(shared::SoundEmitter::MAX_LAYERS)));
      for (uint8_t i = 0; i < emitter.layerCount; i++) {
        emitter.layers[i] = d.soundLayers[i];
      }
      game.registry.template emplace<shared::SoundEmitter>(entity, emitter);
    }
  }
}
