#include "physics_engine.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <cfloat>
#include <cmath>
#include <cstdio>

JPH::BodyID PhysicsEngine::createPlayerBody(float x, float y, float z) {
  auto& bodyInterface = getBodyInterface();

  JPH::CapsuleShapeSettings capsuleSettings(0.5f, 0.5f);
  JPH::ShapeRefC shape = capsuleSettings.Create().Get();

  JPH::BodyCreationSettings settings(shape, JPH::RVec3(x, y, z),
                                     JPH::Quat::sIdentity(),
                                     JPH::EMotionType::Dynamic, Layers::MOVING);
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

JPH::BodyID PhysicsEngine::createFloor() {
  auto& bodyInterface = getBodyInterface();

  JPH::BoxShapeSettings floorShapeSettings(JPH::Vec3(100.0f, 100.0f, 1.0f));
  floorShapeSettings.SetEmbedded();
  JPH::ShapeRefC floorShape = floorShapeSettings.Create().Get();
  JPH::BodyCreationSettings floorSettings(
      floorShape, JPH::RVec3(0.0f, 0.0f, -1.0f), JPH::Quat::sIdentity(),
      JPH::EMotionType::Static, Layers::NON_MOVING);
  JPH::Body* floor = bodyInterface.CreateBody(floorSettings);
  bodyInterface.AddBody(floor->GetID(), JPH::EActivation::DontActivate);
  return floor->GetID();
}

JPH::BodyID PhysicsEngine::createMeshBody(const std::string& filename, float x,
                                          float y, float z, float scale) {
  auto& bodyInterface = getBodyInterface();

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
      filename, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

  if (!scene || !scene->mRootNode) {
    printf("Failed to load mesh for physics: %s\n", filename.c_str());
    JPH::BoxShapeSettings fallback(JPH::Vec3(0.5f, 0.5f, 0.5f));
    fallback.SetEmbedded();
    JPH::ShapeRefC shape = fallback.Create().Get();
    JPH::BodyCreationSettings settings(
        shape, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    JPH::Body* body = bodyInterface.CreateBody(settings);
    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID();
  }

  float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
  float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    const aiMesh* mesh = scene->mMeshes[m];
    for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
      const aiVector3D& vert = mesh->mVertices[v];
      minX = std::min(minX, vert.x);
      minY = std::min(minY, vert.y);
      minZ = std::min(minZ, vert.z);
      maxX = std::max(maxX, vert.x);
      maxY = std::max(maxY, vert.y);
      maxZ = std::max(maxZ, vert.z);
    }
  }

  float halfX = (maxX - minX) / 2.0f * scale;
  float halfY = (maxZ - minZ) / 2.0f * scale;
  float halfZ = (maxY - minY) / 2.0f * scale;
  printf("Mesh: %s\n", filename.c_str());
  printf("halfX=%.3f halfY=%.3f halfZ=%.3f\n", halfX, halfY, halfZ);
  printf("min: %.3f %.3f %.3f\n", minX, minY, minZ);
  printf("max: %.3f %.3f %.3f\n", maxX, maxY, maxZ);
  JPH::BoxShapeSettings boxSettings(JPH::Vec3(halfX, halfY, halfZ));
  boxSettings.SetEmbedded();
  JPH::ShapeRefC shape = boxSettings.Create().Get();
  JPH::BodyCreationSettings settings(
      shape, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
      JPH::EMotionType::Static, Layers::NON_MOVING);
  JPH::Body* body = bodyInterface.CreateBody(settings);
  bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
  return body->GetID();
}