#include "map_loader.h"

#include <assimp/scene.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "physics_engine.h"
#include "server_game.h"
#include "shared/components.h"
#include "shared/lighting.h"
#include "shared/map_format.h"
#include "shared/mesh_loader.h"

namespace {

void decompose(const aiMatrix4x4& m, glm::vec3& pos, glm::quat& rot,
               glm::vec3& scale) {
  aiVector3D t, s;
  aiQuaternion r;
  m.Decompose(s, r, t);
  pos = {t.x, t.y, t.z};
  rot = glm::quat(r.w, r.x, r.y, r.z);
  scale = {s.x, s.y, s.z};
}

// glTF-via-assimp pre-multiplies color by intensity into mColorDiffuse, which
// for Blender point lights produces values in the thousands (lumens). Without
// units the Phong shader just blows out, so for v1 we normalize each color to
// its max channel — relative tints are preserved, brightness is clamped to
// unit. Future work: pipe glTF range/intensity into proper attenuation.
aiColor3D normalizeLightColor(const aiColor3D& c) {
  float m = std::max({c.r, c.g, c.b});
  if (m <= 1.0f || m == 0.0f) return c;
  return {c.r / m, c.g / m, c.b / m};
}

}  // namespace

bool loadMap(ServerGame& game, const std::string& path) {
  shared::ParsedModel parsed;
  if (!parsed.load(path, shared::MAP_LOAD_FLAGS)) {
    printf("loadMap: failed to load \"%s\": %s\n", path.c_str(),
           parsed.lastError().c_str());
    return false;
  }
  const aiScene* scene = parsed.scene();

  unsigned meshEntities = 0;
  unsigned pointLights = 0;
  unsigned dirLights = 0;
  unsigned skippedLights = 0;

  parsed.forEachMeshNode([&](const aiNode& node, const aiMatrix4x4& world) {
    glm::vec3 pos;
    glm::quat rot;
    glm::vec3 scale;
    decompose(world, pos, rot, scale);

    auto [id, entity] = new_entity(game);
    game.registry.emplace<shared::Position>(entity, pos.x, pos.y, pos.z, rot.w,
                                            rot.x, rot.y, rot.z);
    game.registry.emplace<shared::RenderInfo>(
        entity, std::string(shared::MAP_MODEL_PREFIX) + node.mName.C_Str(),
        scale.x, scale.y, scale.z);
    JPH::BodyID bodyId =
        game.physics.createMeshBody(parsed, node, pos, rot, scale);
    if (!bodyId.IsInvalid()) {
      game.registry.emplace<shared::PhysicsBody>(
          entity, bodyId.GetIndexAndSequenceNumber());
    }
    ++meshEntities;
  });

  for (unsigned i = 0; i < scene->mNumLights; ++i) {
    const aiLight* light = scene->mLights[i];
    const aiMatrix4x4* world = parsed.worldTransform(light->mName.C_Str());
    if (!world) {
      printf("loadMap: light \"%s\" has no matching node, skipping\n",
             light->mName.C_Str());
      ++skippedLights;
      continue;
    }
    glm::vec3 pos;
    glm::quat rot;
    glm::vec3 scale;
    decompose(*world, pos, rot, scale);
    aiColor3D color = normalizeLightColor(light->mColorDiffuse);

    if (light->mType == aiLightSource_POINT) {
      // If the glTF artist specified a `range` cutoff, derive attenuation to
      // match. assimp stores it on the node's metadata, not on aiLight.
      shared::PointLightAttenuation att = shared::kDefaultPointLightAttenuation;
      const aiNode* lightNode =
          scene->mRootNode->FindNode(aiString(light->mName));
      if (lightNode && lightNode->mMetaData) {
        double range = 0.0;
        if (lightNode->mMetaData->Get("PBR_LightRange", range) && range > 0.0) {
          att = shared::attenuationForRange(static_cast<float>(range));
        }
      }
      auto [id, entity] = new_entity(game);
      game.registry.emplace<shared::Position>(entity, pos.x, pos.y, pos.z,
                                              rot.w, rot.x, rot.y, rot.z);
      game.registry.emplace<shared::PointLight>(
          entity, pos.x, pos.y, pos.z, att.constant, att.linear, att.quadratic,
          // ambient / diffuse / specular
          color.r * 0.1f, color.g * 0.1f, color.b * 0.1f, color.r, color.g,
          color.b, color.r, color.g, color.b);
      ++pointLights;
    } else if (light->mType == aiLightSource_DIRECTIONAL) {
      // glTF directional light points along the node's local -Z. Rotate that
      // by the world rotation to get a world-space direction.
      glm::vec3 worldDir = rot * glm::vec3(0.0f, 0.0f, -1.0f);
      auto [id, entity] = new_entity(game);
      game.registry.emplace<shared::DirectionalLight>(
          entity, worldDir.x, worldDir.y, worldDir.z,
          // ambient / diffuse / specular
          color.r * 0.1f, color.g * 0.1f, color.b * 0.1f, color.r, color.g,
          color.b, color.r, color.g, color.b);
      ++dirLights;
    } else {
      printf("loadMap: unsupported light type %d on node \"%s\", skipping\n",
             static_cast<int>(light->mType), light->mName.C_Str());
      ++skippedLights;
    }
  }

  printf(
      "loadMap: \"%s\" — spawned %u mesh entities, %u point lights, "
      "%u directional lights (%u skipped)\n",
      path.c_str(), meshEntities, pointLights, dirLights, skippedLights);
  return true;
}
