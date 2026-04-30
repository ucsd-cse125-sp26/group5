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

class aiNode;
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

  // Dynamic player body whose shape is resolved from `modelName` (capsule for
  // the default "cube" model, oriented box for mesh-backed assets like
  // "bear"). Rotation DOFs are locked so physics doesn't tumble the body —
  // user code drives rotation via SetRotation.
  JPH::BodyID createPlayerBody(const std::string& modelName,
                               const glm::vec3& pos, const glm::quat& rot,
                               const glm::vec3& scale);

  // Returns a Jolt shape sized to fit the named asset. Procedural assets
  // (cube) get a unit box; mesh-backed assets (bear) get a box whose AABB is
  // computed in post-orientation, post-hierarchy space and cached per asset.
  // The asset's render-time orientation is BAKED INTO the shape so the body's
  // rotation can stay equal to the entity's rotation.
  //
  // `centerOffsetMask` is multiplied per-axis with the AABB's local-space
  // center offset before baking it into the shape (via RotatedTranslatedShape):
  //   - (1,1,1) [default]: full offset — box matches the asset's natural mesh
  //     position. Right for static bodies whose visual+collision share the
  //     same logical anchor.
  //   - (0,0,1): vertical offset only — box's bottom aligns with the asset's
  //     "feet" but XY is centered on the body. Right for player bodies, where
  //     the movement pivot must be on the body origin but vertical alignment
  //     with the visual matters so the player stands on the ground.
  JPH::ShapeRefC boxShapeForAsset(
      const std::string& modelName, const glm::vec3& scale,
      const glm::vec3& centerOffsetMask = glm::vec3(1.0f));

  // Triangle-mesh shape of the asset's geometry. Caches the unscaled
  // JPH::MeshShape per asset and wraps it in JPH::ScaledShape per call.
  // Asset orientation is baked into vertices, same as boxShapeForAsset.
  // Returns nullptr if the asset is procedural (no file) or has no triangles.
  JPH::ShapeRefC meshShapeForAsset(const std::string& modelName,
                                   const glm::vec3& scale);

  // Returns the player-friendly shape for an asset: capsule for procedural
  // (cube) assets, oriented box (via boxShapeForAsset) for mesh-backed assets.
  JPH::ShapeRefC playerShapeForAsset(const std::string& modelName,
                                     const glm::vec3& scale);

  // Adds a static body with the given shape at (pos, rot). Returns its
  // BodyID. Layer is NON_MOVING; activation is DontActivate.
  JPH::BodyID createStaticBody(const JPH::ShapeRefC& shape,
                               const glm::vec3& pos, const glm::quat& rot);

  // Static box body of the given local half-extents, placed at (pos, rot).
  // Used for procedural geometry and other "I already know the dimensions"
  // call sites that don't have a ParsedModel. `localCenterOffset` shifts the
  // box's center within the body's local frame — use it when the desired
  // collision volume isn't centered on the body's pivot, so the body's world
  // position can stay equal to the entity's Position (avoiding the per-tick
  // sync writing an offset value back into Position).
  JPH::BodyID createBoxBody(
      const glm::vec3& halfExtents, const glm::vec3& pos, const glm::quat& rot,
      const glm::vec3& localCenterOffset = glm::vec3(0.0f));

  // Static triangle-mesh body for the geometry of `node` in `parsed`. Caches
  // the unscaled JPH::MeshShape per (model path, node name) and applies the
  // per-call scale via JPH::ScaledShape so the same source mesh can spawn
  // any number of differently-scaled bodies without re-tessellation.
  // Returns an invalid BodyID if the node has no triangles.
  JPH::BodyID createMeshBody(const shared::ParsedModel& parsed,
                             const aiNode& node, const glm::vec3& pos,
                             const glm::quat& rot, const glm::vec3& scale);

  // Static AABB-box body sized to the local-space bounds of `node`'s meshes,
  // then placed at `pos` with `rot` (the box is centered on the local AABB
  // center, not the local origin, so off-center geometry collides correctly).
  // Caches the local AABB per (model path, node name).
  JPH::BodyID createBoxBody(const shared::ParsedModel& parsed,
                            const aiNode& node, const glm::vec3& pos,
                            const glm::quat& rot, const glm::vec3& scale);

 private:
  // Cache key: parsed_model_path + ":" + node_name.
  struct BoxExtents {
    glm::vec3 center;       // local-space center of AABB
    glm::vec3 halfExtents;  // local-space half-extents
  };
  std::unordered_map<std::string, JPH::ShapeRefC> meshShapeCache_;
  std::unordered_map<std::string, BoxExtents> boxExtentsCache_;

  // Per-asset caches keyed by modelName. Asset orientation is baked into the
  // stored geometry so callers don't need to track it separately.
  std::unordered_map<std::string, BoxExtents> assetBoxCache_;
  std::unordered_map<std::string, JPH::ShapeRefC> assetMeshCache_;

  BPLayerInterfaceImpl broadPhaseLayerInterface_;
  ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter_;
  ObjectLayerPairFilterImpl objectLayerPairFilter_;
  JPH::TempAllocatorImpl* tempAllocator_ = nullptr;
  JPH::JobSystemThreadPool* jobSystem_ = nullptr;
};
