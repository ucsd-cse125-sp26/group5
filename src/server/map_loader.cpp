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
#include "shared/map_gamelogic_layout.h"
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

// glTF-via-assimp pre-multiplies intensity into mColorDiffuse (lumens for
// Blender point lights), which blows out the Phong shader. Normalize to
// preserve relative tint until proper range/intensity attenuation lands.
aiColor3D normalizeLightColor(const aiColor3D& c) {
  float m = std::max({c.r, c.g, c.b});
  if (m <= 1.0f || m == 0.0f) return c;
  return {c.r / m, c.g / m, c.b / m};
}

}  // namespace

bool loadMap(ServerGame& game, const std::string& path,
             const std::function<void(ServerGame&, entt::entity)>& tagEntity) {
  shared::ParsedModel parsed;
  if (!parsed.load(path, shared::MAP_LOAD_FLAGS)) {
    printf("loadMap: failed to load \"%s\": %s\n", path.c_str(),
           parsed.lastError().c_str());
    return false;
  }
  const aiScene* scene = parsed.scene();
  auto tag = [&](entt::entity e) {
    if (tagEntity) tagEntity(game, e);
  };

  unsigned meshEntities = 0;
  unsigned pointLights = 0;
  unsigned skippedLights = 0;

  parsed.forEachMeshNode([&](const aiNode& node, const aiMatrix4x4& world) {
    glm::vec3 pos;
    glm::quat rot;
    glm::vec3 scale;
    decompose(world, pos, rot, scale);

    auto [id, entity] = new_entity(game);
    tag(entity);
    game.registry.emplace<shared::Position>(entity, pos.x, pos.y, pos.z, rot.w,
                                            rot.x, rot.y, rot.z);
    game.registry.emplace<shared::RenderInfo>(
        entity, std::string(shared::MAP_MODEL_PREFIX) + node.mName.C_Str(),
        scale.x, scale.y, scale.z);
    auto& renderInfo = game.registry.emplace<shared::RenderInfo>(
        entity, std::string(shared::MAP_MODEL_PREFIX) + node.mName.C_Str(),
        scale.x, scale.y, scale.z);
    // Autumn play platform stays in full color regardless of restoration.
    if (std::string(node.mName.C_Str()) == "autumn_platform") {
      renderInfo.colorExempt = true;
    }
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
      // glTF `range` lives on node metadata, not aiLight.
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
      tag(entity);
      game.registry.emplace<shared::Position>(entity, pos.x, pos.y, pos.z,
                                              rot.w, rot.x, rot.y, rot.z);
      game.registry.emplace<shared::PointLight>(
          entity, pos.x, pos.y, pos.z, att.constant, att.linear, att.quadratic,
          // ambient / diffuse / specular
          color.r * 0.1f, color.g * 0.1f, color.b * 0.1f, color.r, color.g,
          color.b, color.r, color.g, color.b);
      ++pointLights;
    } else if (light->mType == aiLightSource_DIRECTIONAL) {
      // Directional lighting comes from the scene's SceneInfo, not the map.
      ++skippedLights;
    } else {
      printf("loadMap: unsupported light type %d on node \"%s\", skipping\n",
             static_cast<int>(light->mType), light->mName.C_Str());
      ++skippedLights;
    }
  }

  shared::map_gamelogic_layout::tryApplyMazeLayoutFromMap(parsed,
                                                          game.mazeLayout);
  game.mazeLayout.applyHeightBoost();
  shared::map_gamelogic_layout::tryApplyFallLayoutFromMap(parsed,
                                                          game.fallLayout);
  shared::map_gamelogic_layout::tryApplyTangramArenaFromMap(parsed,
                                                            game.tangramArena);
  game.tangramArena.applyHeightBoost();
  shared::map_gamelogic_layout::tryApplyTangramSlotsFromMap(
      parsed, game.tangramArena.boardCenterX, game.tangramArena.boardCenterY,
      game.tangramSlotLayout);
  shared::map_gamelogic_layout::tryApplyFallenHouseRegionFromMap(
      parsed, game.fallenHouseRegion);

  printf(
      "loadMap: \"%s\" — spawned %u mesh entities, %u point lights "
      "(%u skipped)\n",
      path.c_str(), meshEntities, pointLights, skippedLights);
  return true;
}
