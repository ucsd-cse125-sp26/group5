#pragma once

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

class ServerGame;

enum class CollisionShape {
  // AABB-box derived from the asset's post-orientation bounds. Cheap.
  Box,
  // Triangle-mesh body using the asset's actual geometry. Accurate but only
  // available for mesh-backed assets (procedural cubes can't use this).
  Mesh,
};

// Declarative description of a static entity (e.g. floor, ad-hoc props).
// One desc → one ECS entity with Position + (optional) RenderInfo +
// (optional) PhysicsBody. Render and physics share the same `modelName` and
// `scale` so a single source of truth drives both. Asset orientation
// (shared::ASSETS[].q*) is applied to the physics shape so collision lines
// up with the visible mesh.
struct StaticEntityDesc {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  std::string modelName;  // "" → no visual entity
  glm::vec3 scale{1.0f, 1.0f, 1.0f};
  CollisionShape collision = CollisionShape::Box;
};

void spawnStaticEntities(ServerGame& game,
                         const std::vector<StaticEntityDesc>& descs);
