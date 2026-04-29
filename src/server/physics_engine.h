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
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
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

  JPH::BodyID createPlayerBody(float x, float y, float z);
  JPH::BodyID createFloor();
  JPH::BodyID createMeshBody(const std::string& filename,
                              float x, float y, float z, float scale = 1.0f);

 private:
  BPLayerInterfaceImpl broadPhaseLayerInterface_;
  ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter_;
  ObjectLayerPairFilterImpl objectLayerPairFilter_;
  JPH::TempAllocatorImpl* tempAllocator_ = nullptr;
  JPH::JobSystemThreadPool* jobSystem_ = nullptr;
};
