#pragma once

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

class ServerGame;

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

void spawnStaticEntities(ServerGame& game,
                         const std::vector<StaticEntityDesc>& descs);
