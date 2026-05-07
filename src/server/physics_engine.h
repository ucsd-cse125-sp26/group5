#pragma once
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <unordered_map>

struct aiNode;
namespace shared {
class ParsedModel;
}
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdint>
#include <string>
#include <thread>

// Jolt requires you to define object layers
namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};  // namespace Layers

// Tells Jolt which layers can collide with each other
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
 public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a,
                                   JPH::ObjectLayer b) const override {
    switch (a) {
      case Layers::NON_MOVING:
        return b == Layers::MOVING;
      case Layers::MOVING:
        return true;
      default:
        return false;
    }
  }
};

// Broadphase layers (coarse collision detection)
namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM_LAYERS(2);
};  // namespace BroadPhaseLayers

class BPLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface {
 public:
  BPLayerInterfaceImpl() {
    mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }
  [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }
  [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(
      JPH::ObjectLayer layer) const override {
    JPH_ASSERT(layer < Layers::NUM_LAYERS);
    return mObjectToBroadPhase[layer];
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(
      JPH::BroadPhaseLayer layer) const override {
    switch ((JPH::BroadPhaseLayer::Type)layer) {
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
        return "NON_MOVING";
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
        return "MOVING";
      default:
        return "UNKNOWN";
    }
  }
#endif

 private:
  JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl
    : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  [[nodiscard]] bool ShouldCollide(
      JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const override {
    switch (layer) {
      case Layers::NON_MOVING:
        return bpLayer == BroadPhaseLayers::MOVING;
      case Layers::MOVING:
        return true;
      default:
        return false;
    }
  }
};

class PhysicsEngine {
 public:
  JPH::PhysicsSystem physicsSystem;

  PhysicsEngine() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    tempAllocator_ = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    jobSystem_ = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        std::thread::hardware_concurrency() - 1);
    physicsSystem.Init(1024, 0, 1024, 1024, broadPhaseLayerInterface_,
                       objectVsBroadPhaseLayerFilter_, objectLayerPairFilter_);
    physicsSystem.SetGravity(JPH::Vec3(0.0f, 0.0f, -18.0f));
  }

  ~PhysicsEngine() {
    delete tempAllocator_;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    delete jobSystem_;
  }

  void step(float dt) {
    physicsSystem.Update(dt, 1, tempAllocator_, jobSystem_);
  }

  JPH::BodyInterface& getBodyInterface() {
    return physicsSystem.GetBodyInterface();
  }

  void destroyBody(uint32_t bodyId) {
    JPH::BodyID joltId(bodyId);
    getBodyInterface().RemoveBody(joltId);
    getBodyInterface().DestroyBody(joltId);
  }

  // Dynamic player body. Rotation DOFs are locked; rotation is driven by
  // SetRotation, not physics.
  JPH::BodyID createPlayerBody(const std::string& modelName,
                               const glm::vec3& pos, const glm::quat& rot,
                               const glm::vec3& scale);

  // Asset orientation is baked into the shape so the body's rotation can
  // stay equal to the entity's rotation. `centerOffsetMask` is multiplied
  // per-axis with the AABB's local-space center before baking — use (0,0,1)
  // for player bodies (vertical alignment only; XY collision pivot stays
  // on the body origin so movement pivot matches).
  JPH::ShapeRefC boxShapeForAsset(
      const std::string& modelName, const glm::vec3& scale,
      const glm::vec3& centerOffsetMask = glm::vec3(1.0f));

  // Returns nullptr if the asset is procedural (no triangle source).
  JPH::ShapeRefC meshShapeForAsset(const std::string& modelName,
                                   const glm::vec3& scale);

  JPH::ShapeRefC playerShapeForAsset(const std::string& modelName,
                                     const glm::vec3& scale);

  JPH::BodyID createStaticBody(const JPH::ShapeRefC& shape,
                               const glm::vec3& pos, const glm::quat& rot);

  // `localCenterOffset` keeps the body at `pos` while shifting the collision
  // volume — avoids the per-tick sync writing an offset back into Position.
  JPH::BodyID createBoxBody(
      const glm::vec3& halfExtents, const glm::vec3& pos, const glm::quat& rot,
      const glm::vec3& localCenterOffset = glm::vec3(0.0f));

  // Caches the unscaled MeshShape per (model path, node name); per-call
  // scale is applied via ScaledShape.
  JPH::BodyID createMeshBody(const shared::ParsedModel& parsed,
                             const aiNode& node, const glm::vec3& pos,
                             const glm::quat& rot, const glm::vec3& scale);

  // Box centered on the local AABB so off-center node geometry collides
  // correctly. Caches the local AABB per (model path, node name).
  JPH::BodyID createBoxBody(const shared::ParsedModel& parsed,
                            const aiNode& node, const glm::vec3& pos,
                            const glm::quat& rot, const glm::vec3& scale);

 private:
  struct BoxExtents {
    glm::vec3 center;
    glm::vec3 halfExtents;
  };
  std::unordered_map<std::string, JPH::ShapeRefC> meshShapeCache_;
  std::unordered_map<std::string, BoxExtents> boxExtentsCache_;

  std::unordered_map<std::string, BoxExtents> assetBoxCache_;
  std::unordered_map<std::string, JPH::ShapeRefC> assetMeshCache_;

  BPLayerInterfaceImpl broadPhaseLayerInterface_;
  ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter_;
  ObjectLayerPairFilterImpl objectLayerPairFilter_;
  JPH::TempAllocatorImpl* tempAllocator_ = nullptr;
  JPH::JobSystemThreadPool* jobSystem_ = nullptr;
};
