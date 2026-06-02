#include "physics_engine.h"

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
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

  // Compute foot offset: distance from body origin down to shape bottom.
  // boxShapeForAsset with Z-only mask offsets box center up by
  // center.z*scale.z, so body origin is center.z*scale.z below the box center,
  // meaning bottom of box relative to origin = center.z*scale.z -
  // halfExtents.z*scale.z = (center.z - halfExtents.z) * scale.z The foot is at
  // body_pos.z + that value (negative = below origin).
  const BoxExtents& ext =
      assetBoxCache_.count(modelName)
          ? assetBoxCache_.at(modelName)
          : BoxExtents{.center = glm::vec3(0), .halfExtents = glm::vec3(0.5f)};
  float footOffset = (ext.center.z - ext.halfExtents.z) * scale.z;
  // footOffset is negative (below origin), so dist from origin to foot =
  // -footOffset

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
  bodyFootOffset_[body->GetID().GetIndexAndSequenceNumber()] = footOffset;
  return body->GetID();
}

JPH::BodyID PhysicsEngine::createMazeBoardPieceBody(
    const std::string& modelName, const glm::vec3& pos, const glm::quat& rot,
    const glm::vec3& scale) {
  auto& bodyInterface = getBodyInterface();
  JPH::ShapeRefC shape = playerShapeForAsset(modelName, scale);

  JPH::Quat joltRot(rot.x, rot.y, rot.z, rot.w);
  JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z),
                                     joltRot, JPH::EMotionType::Dynamic,
                                     Layers::MOVING);
  settings.mGravityFactor = 0.0f;
  settings.mFriction = 0.8f;
  settings.mAllowedDOFs =
      JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationZ;
  settings.mMotionQuality = JPH::EMotionQuality::Discrete;

  JPH::Body* body = bodyInterface.CreateBody(settings);
  return body->GetID();
}

JPH::BodyID PhysicsEngine::createTangramPieceBody(
    const std::string& modelName, const glm::vec3& pos, const glm::quat& rot,
    const glm::vec3& scale, JPH::ObjectLayer objectLayer) {
  auto& bodyInterface = getBodyInterface();
  JPH::ShapeRefC shape = playerShapeForAsset(modelName, scale);

  JPH::Quat joltRot(rot.x, rot.y, rot.z, rot.w);
  JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z),
                                     joltRot, JPH::EMotionType::Dynamic,
                                     objectLayer);
  settings.mGravityFactor = 0.0f;
  // Heavy slabs: hard to shove, high damping kills glide after you stop
  // pushing.
  settings.mOverrideMassProperties =
      JPH::EOverrideMassProperties::CalculateInertia;
  settings.mMassPropertiesOverride.mMass = 550.0f;
  settings.mFriction = 1.15f;
  settings.mRestitution = 0.0f;
  settings.mLinearDamping = 48.0f;
  settings.mAngularDamping = 10.0f;
  settings.mMaxLinearVelocity = 1.6f;
  settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                          JPH::EAllowedDOFs::TranslationY |
                          JPH::EAllowedDOFs::RotationZ;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

  JPH::Body* body = bodyInterface.CreateBody(settings);
  return body->GetID();
}

JPH::BodyID PhysicsEngine::createFallingObjectBody(const glm::vec3& halfExtents,
                                                   const glm::vec3& pos) {
  auto& bodyInterface = getBodyInterface();
  JPH::BoxShapeSettings boxSettings(
      JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
  boxSettings.SetEmbedded();
  JPH::ShapeRefC shape = boxSettings.Create().Get();

  JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z),
                                     JPH::Quat::sIdentity(),
                                     JPH::EMotionType::Dynamic, Layers::MOVING);
  settings.mGravityFactor = 1.0f;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

  JPH::Body* body = bodyInterface.CreateBody(settings);
  bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
  return body->GetID();
}

namespace {

std::string cacheKey(const shared::ParsedModel& parsed, const aiNode& node) {
  return parsed.path() + ":" + node.mName.C_Str();
}

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

// Jolt's narrow-phase (EPA) crashes on slivers its own sanitizer misses
// (JoltPhysics #1352). 1e-10 ≈ 5 µm² area — well below anything physical.
size_t pruneDegenerateTriangles(JPH::TriangleList& tris) {
  constexpr float kMinCrossSq = 1e-10f;
  auto finite3 = [](const JPH::Float3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  size_t kept = 0;
  for (size_t i = 0; i < tris.size(); ++i) {
    const auto& t = tris[i];
    if (!finite3(t.mV[0]) || !finite3(t.mV[1]) || !finite3(t.mV[2])) continue;
    glm::vec3 a(t.mV[0].x, t.mV[0].y, t.mV[0].z);
    glm::vec3 b(t.mV[1].x, t.mV[1].y, t.mV[1].z);
    glm::vec3 c(t.mV[2].x, t.mV[2].y, t.mV[2].z);
    glm::vec3 cross = glm::cross(b - a, c - a);
    if (glm::dot(cross, cross) < kMinCrossSq) continue;
    if (kept != i) tris[kept] = t;
    ++kept;
  }
  size_t dropped = tris.size() - kept;
  tris.resize(kept);
  return dropped;
}

JPH::ShapeRefC buildMeshShape(JPH::TriangleList&& tris, const char* tag) {
  if (tris.empty()) return nullptr;
  size_t dropped = pruneDegenerateTriangles(tris);
  if (dropped > 0) {
    printf("MeshShape: pruned %zu degenerate triangle(s) from %s\n", dropped,
           tag);
  }
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
                                            const glm::quat& rot,
                                            float frictionOrNegative) {
  JPH::Quat joltRot(rot.x, rot.y, rot.z, rot.w);
  JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z),
                                     joltRot, JPH::EMotionType::Static,
                                     Layers::NON_MOVING);
  if (frictionOrNegative >= 0.0f) {
    settings.mFriction = frictionOrNegative;
  }
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

  // Center offset goes inside the shape (RotatedTranslatedShape) so the
  // body stays at `pos` and Jolt→ECS sync doesn't drift Position.
  return createBoxBody(ext.halfExtents * scale, pos, rot, ext.center * scale);
}

// Anything below ~1e-3 or non-finite crashes Jolt's contact resolver.
static bool isUsableScale(const glm::vec3& s) {
  constexpr float kMinScale = 1e-3f;
  return std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z) &&
         s.x > kMinScale && s.y > kMinScale && s.z > kMinScale;
}

JPH::ShapeRefC PhysicsEngine::boxShapeForAsset(
    const std::string& modelName, const glm::vec3& scale,
    const glm::vec3& centerOffsetMask) {
  if (!isUsableScale(scale)) {
    printf("boxShapeForAsset: rejecting bad scale (%g, %g, %g) for %s\n",
           scale.x, scale.y, scale.z, modelName.c_str());
    return nullptr;
  }
  BoxExtents ext;
  if (auto it = assetBoxCache_.find(modelName); it != assetBoxCache_.end()) {
    ext = it->second;
  } else {
    const auto* asset = shared::findAsset(modelName);
    if (asset && !asset->filename.empty()) {
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
      // Procedural asset (cube, light_cube).
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
  if (!isUsableScale(scale)) {
    printf("meshShapeForAsset: rejecting bad scale (%g, %g, %g) for %s\n",
           scale.x, scale.y, scale.z, modelName.c_str());
    return nullptr;
  }
  JPH::ShapeRefC unscaled;
  if (auto it = assetMeshCache_.find(modelName); it != assetMeshCache_.end()) {
    unscaled = it->second;
  } else {
    const auto* asset = shared::findAsset(modelName);
    if (!asset || asset->filename.empty()) return nullptr;
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
  // Z-only center offset: aligns the asset's mesh-origin feet with the box
  // bottom. Without it the bear settles 9 units above the floor (and a
  // previous floor-penetration segfaulted Jolt's contact resolver). XY
  // offsets are dropped so the collision pivot matches movement pivot.
  return boxShapeForAsset(modelName, scale, glm::vec3(0.0f, 0.0f, 1.0f));
}

bool PhysicsEngine::isBodyGrounded(JPH::BodyID bodyId, float checkDistance) {
  auto& bodyInterface = getBodyInterface();
  if (!bodyInterface.IsAdded(bodyId)) return false;

  // GetTransformedShape gives us the shape with the body's current world
  // transform, then GetWorldSpaceBounds accounts for RotatedTranslatedShape,
  // ScaledShape, etc.
  JPH::TransformedShape ts = bodyInterface.GetTransformedShape(bodyId);
  JPH::AABox bounds = ts.GetWorldSpaceBounds();

  // Ray starts just inside the shape bottom, travels checkDistance down
  JPH::RVec3 rayOrigin(bounds.GetCenter().GetX(), bounds.GetCenter().GetY(),
                       bounds.mMin.GetZ() + 0.01f);
  JPH::Vec3 down(0.0f, 0.0f, -1.0f);
  JPH::RRayCast ray{rayOrigin, down * (checkDistance + 0.01f)};

  JPH::RayCastResult result;
  return physicsSystem.GetNarrowPhaseQuery().CastRay(
      ray, result,
      JPH::SpecifiedBroadPhaseLayerFilter(BroadPhaseLayers::NON_MOVING),
      JPH::SpecifiedObjectLayerFilter(Layers::NON_MOVING));
}

bool PhysicsEngine::getBodyWorldZSpan(JPH::BodyID bodyId, float& minZ,
                                      float& maxZ) {
  auto& bodyInterface = getBodyInterface();
  if (!bodyInterface.IsAdded(bodyId)) return false;
  // Same path isBodyGrounded uses: GetTransformedShape applies the body's
  // world transform, and GetWorldSpaceBounds folds in RotatedTranslatedShape /
  // ScaledShape so the AABB reflects the real, scaled, offset collision box.
  JPH::AABox bounds =
      bodyInterface.GetTransformedShape(bodyId).GetWorldSpaceBounds();
  minZ = bounds.mMin.GetZ();
  maxZ = bounds.mMax.GetZ();
  return true;
}
