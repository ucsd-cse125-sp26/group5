#include "physics_engine.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "shared/assets.h"
#include "shared/mesh_loader.h"

JPH::BodyID PhysicsEngine::createPlayerBody(const std::string& modelName,
                                            const glm::vec3& pos,
                                            const glm::quat& rot,
                                            const glm::vec3& scale) {
  auto& bodyInterface = getBodyInterface();
  JPH::ShapeRefC shape = playerShapeForAsset(modelName, scale);

  JPH::Quat joltRot(rot.x, rot.y, rot.z, rot.w);
  JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z),
                                     joltRot, JPH::EMotionType::Dynamic,
                                     Layers::MOVING);
  settings.mGravityFactor = 1.0f;
  settings.mFriction = 0.5f;
  settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                          JPH::EAllowedDOFs::TranslationY |
                          JPH::EAllowedDOFs::TranslationZ;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

  JPH::Body* body = bodyInterface.CreateBody(settings);
  bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
  return body->GetID();
}

namespace {

// Cache key combines the parsed model's source path with the node name so the
// same node from the same file always hits the cache, regardless of the
// per-call transform.
std::string cacheKey(const shared::ParsedModel& parsed, const aiNode& node) {
  return parsed.path() + ":" + node.mName.C_Str();
}

// Accumulates the AABB of the meshes referenced by `node`. `xform` maps each
// vertex from mesh-local space to the desired space (use identity to keep
// mesh-local). Updates `mn`/`mx` in place so callers can iterate over many
// nodes. Returns true if any vertex was processed.
template <typename Xform>
bool accumulateMeshAABB(const aiNode& node, const aiScene& scene, Xform xform,
                        glm::vec3& mn, glm::vec3& mx) {
  bool any = false;
  for (unsigned i = 0; i < node.mNumMeshes; ++i) {
    const aiMesh* mesh = scene.mMeshes[node.mMeshes[i]];
    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
      glm::vec3 p = xform(mesh->mVertices[v]);
      mn = glm::min(mn, p);
      mx = glm::max(mx, p);
      any = true;
    }
  }
  return any;
}

// Wraps `tris` in a Jolt MeshShape, logging build failures with `tag`.
// Returns nullptr on empty input or build error.
JPH::ShapeRefC buildMeshShape(JPH::TriangleList&& tris, const char* tag) {
  if (tris.empty()) return nullptr;
  JPH::MeshShapeSettings settings(tris);
  settings.SetEmbedded();
  auto result = settings.Create();
  if (result.HasError()) {
    printf("MeshShape build failed for %s: %s\n", tag,
           result.GetError().c_str());
    return nullptr;
  }
  return result.Get();
}

}  // namespace

JPH::BodyID PhysicsEngine::createStaticBody(const JPH::ShapeRefC& shape,
                                            const glm::vec3& pos,
                                            const glm::quat& rot) {
  JPH::Quat joltRot(rot.x, rot.y, rot.z, rot.w);
  JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z),
                                     joltRot, JPH::EMotionType::Static,
                                     Layers::NON_MOVING);
  JPH::Body* body = getBodyInterface().CreateBody(settings);
  getBodyInterface().AddBody(body->GetID(), JPH::EActivation::DontActivate);
  return body->GetID();
}

JPH::BodyID PhysicsEngine::createBoxBody(const glm::vec3& halfExtents,
                                         const glm::vec3& pos,
                                         const glm::quat& rot,
                                         const glm::vec3& localCenterOffset) {
  JPH::BoxShapeSettings boxSettings(
      JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
  boxSettings.SetEmbedded();
  JPH::ShapeRefC shape = boxSettings.Create().Get();
  if (glm::dot(localCenterOffset, localCenterOffset) > 1e-12f) {
    JPH::RotatedTranslatedShapeSettings rt(
        JPH::Vec3(localCenterOffset.x, localCenterOffset.y,
                  localCenterOffset.z),
        JPH::Quat::sIdentity(), shape);
    rt.SetEmbedded();
    shape = rt.Create().Get();
  }
  return createStaticBody(shape, pos, rot);
}

JPH::BodyID PhysicsEngine::createMeshBody(const shared::ParsedModel& parsed,
                                          const aiNode& node,
                                          const glm::vec3& pos,
                                          const glm::quat& rot,
                                          const glm::vec3& scale) {
  const std::string key = cacheKey(parsed, node);
  JPH::ShapeRefC unscaled;
  if (auto it = meshShapeCache_.find(key); it != meshShapeCache_.end()) {
    unscaled = it->second;
  } else {
    std::vector<aiVector3D> verts;
    std::vector<uint32_t> indices;
    shared::flattenNodeGeometry(node, *parsed.scene(), verts, indices);
    JPH::TriangleList tris;
    tris.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      const auto& v0 = verts[indices[i + 0]];
      const auto& v1 = verts[indices[i + 1]];
      const auto& v2 = verts[indices[i + 2]];
      tris.emplace_back(JPH::Float3(v0.x, v0.y, v0.z),
                        JPH::Float3(v1.x, v1.y, v1.z),
                        JPH::Float3(v2.x, v2.y, v2.z));
    }
    unscaled = buildMeshShape(std::move(tris), key.c_str());
    if (!unscaled) return {};
    meshShapeCache_[key] = unscaled;
  }

  JPH::ScaledShapeSettings scaledSettings(unscaled,
                                          JPH::Vec3(scale.x, scale.y, scale.z));
  scaledSettings.SetEmbedded();
  auto sresult = scaledSettings.Create();
  if (sresult.HasError()) {
    printf("ScaledShape build failed for %s: %s\n", key.c_str(),
           sresult.GetError().c_str());
    return {};
  }
  return createStaticBody(sresult.Get(), pos, rot);
}

JPH::BodyID PhysicsEngine::createBoxBody(const shared::ParsedModel& parsed,
                                         const aiNode& node,
                                         const glm::vec3& pos,
                                         const glm::quat& rot,
                                         const glm::vec3& scale) {
  const std::string key = cacheKey(parsed, node);
  BoxExtents ext;
  if (auto it = boxExtentsCache_.find(key); it != boxExtentsCache_.end()) {
    ext = it->second;
  } else {
    glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
    auto identity = [](const aiVector3D& v) {
      return glm::vec3(v.x, v.y, v.z);
    };
    if (!accumulateMeshAABB(node, *parsed.scene(), identity, mn, mx)) {
      return {};
    }
    ext.center = (mn + mx) * 0.5f;
    ext.halfExtents = (mx - mn) * 0.5f;
    boxExtentsCache_[key] = ext;
  }

  // Body stays at `pos` (matching the ECS entity's Position); the AABB
  // center offset lives inside the shape via RotatedTranslatedShape so the
  // per-tick Jolt→ECS sync doesn't write an offset value back into Position.
  return createBoxBody(ext.halfExtents * scale, pos, rot, ext.center * scale);
}

JPH::ShapeRefC PhysicsEngine::boxShapeForAsset(
    const std::string& modelName, const glm::vec3& scale,
    const glm::vec3& centerOffsetMask) {
  // Cache key is just modelName since asset orientation is baked in.
  BoxExtents ext;
  if (auto it = assetBoxCache_.find(modelName); it != assetBoxCache_.end()) {
    ext = it->second;
  } else {
    const auto* asset = shared::findAsset(modelName);
    if (asset && !asset->filename.empty()) {
      // Mesh-backed asset: load and walk hierarchy with orientation applied.
      glm::quat orient(asset->qw, asset->qx, asset->qy, asset->qz);
      shared::ParsedModel parsed;
      if (!parsed.load(
              std::string(asset->filename),
              aiProcess_Triangulate | aiProcess_JoinIdenticalVertices)) {
        printf("boxShapeForAsset: failed to load %s for %s — using unit box\n",
               std::string(asset->filename).c_str(), modelName.c_str());
        ext = {.center = glm::vec3(0.0f), .halfExtents = glm::vec3(0.5f)};
      } else {
        glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
        bool any = false;
        parsed.forEachMeshNode(
            [&](const aiNode& node, const aiMatrix4x4& world) {
              auto xform = [&](const aiVector3D& v) {
                aiVector3D wp = world * v;
                return orient * glm::vec3(wp.x, wp.y, wp.z);
              };
              any |= accumulateMeshAABB(node, *parsed.scene(), xform, mn, mx);
            });
        ext = any ? BoxExtents{.center = (mn + mx) * 0.5f,
                               .halfExtents = (mx - mn) * 0.5f}
                  : BoxExtents{.center = glm::vec3(0.0f),
                               .halfExtents = glm::vec3(0.5f)};
      }
    } else {
      // Procedural asset (cube, light_cube): unit box centered at origin.
      ext = {.center = glm::vec3(0.0f), .halfExtents = glm::vec3(0.5f)};
    }
    assetBoxCache_[modelName] = ext;
  }

  glm::vec3 halfExtents = ext.halfExtents * scale;
  glm::vec3 offset = ext.center * scale * centerOffsetMask;
  JPH::BoxShapeSettings boxSettings(
      JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
  boxSettings.SetEmbedded();
  JPH::ShapeRefC shape = boxSettings.Create().Get();
  if (glm::dot(offset, offset) > 1e-12f) {
    JPH::RotatedTranslatedShapeSettings rt(
        JPH::Vec3(offset.x, offset.y, offset.z), JPH::Quat::sIdentity(), shape);
    rt.SetEmbedded();
    shape = rt.Create().Get();
  }
  return shape;
}

JPH::ShapeRefC PhysicsEngine::meshShapeForAsset(const std::string& modelName,
                                                const glm::vec3& scale) {
  JPH::ShapeRefC unscaled;
  if (auto it = assetMeshCache_.find(modelName); it != assetMeshCache_.end()) {
    unscaled = it->second;
  } else {
    const auto* asset = shared::findAsset(modelName);
    if (!asset || asset->filename.empty()) {
      return nullptr;  // procedural/unknown — no triangle mesh available
    }
    glm::quat orient(asset->qw, asset->qx, asset->qy, asset->qz);
    shared::ParsedModel parsed;
    if (!parsed.load(std::string(asset->filename),
                     aiProcess_Triangulate | aiProcess_JoinIdenticalVertices)) {
      printf("meshShapeForAsset: failed to load %s for %s\n",
             std::string(asset->filename).c_str(), modelName.c_str());
      return nullptr;
    }
    JPH::TriangleList tris;
    parsed.forEachMeshNode([&](const aiNode& node, const aiMatrix4x4& world) {
      for (unsigned m = 0; m < node.mNumMeshes; ++m) {
        const aiMesh* mesh = parsed.scene()->mMeshes[node.mMeshes[m]];
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
          const aiFace& face = mesh->mFaces[f];
          if (face.mNumIndices != 3) continue;
          aiVector3D v[3] = {
              world * mesh->mVertices[face.mIndices[0]],
              world * mesh->mVertices[face.mIndices[1]],
              world * mesh->mVertices[face.mIndices[2]],
          };
          glm::vec3 p[3];
          for (int k = 0; k < 3; ++k) {
            p[k] = orient * glm::vec3(v[k].x, v[k].y, v[k].z);
          }
          tris.emplace_back(JPH::Float3(p[0].x, p[0].y, p[0].z),
                            JPH::Float3(p[1].x, p[1].y, p[1].z),
                            JPH::Float3(p[2].x, p[2].y, p[2].z));
        }
      }
    });
    unscaled = buildMeshShape(std::move(tris), modelName.c_str());
    if (!unscaled) return nullptr;
    assetMeshCache_[modelName] = unscaled;
  }
  JPH::ScaledShapeSettings scaledSettings(unscaled,
                                          JPH::Vec3(scale.x, scale.y, scale.z));
  scaledSettings.SetEmbedded();
  return scaledSettings.Create().Get();
}

JPH::ShapeRefC PhysicsEngine::playerShapeForAsset(const std::string& modelName,
                                                  const glm::vec3& scale) {
  // Both procedural and mesh-backed players use a box. Vertical (Z) center
  // offset is baked in so the asset's "feet" align with the box's bottom —
  // the bear's mesh has its origin at its feet, and without this Z offset
  // the body would settle 9 units above the ground (and the deep floor
  // penetration on swap segfaulted Jolt's contact resolver). XY offsets are
  // dropped so the collision pivot matches the player's movement pivot.
  return boxShapeForAsset(modelName, scale,
                          /*centerOffsetMask=*/glm::vec3(0.0f, 0.0f, 1.0f));
}