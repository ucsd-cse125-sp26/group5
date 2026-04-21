#pragma once
#include <enet/enet.h>

#include <cstdint>
#include <entt/entt.hpp>
#include <map>
#include <vector>

#include "shared/component_registry.h"
#include "shared/protocol.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <thread>

class ServerNetwork;

// Jolt requires you to define object layers
namespace Layers {
  static constexpr JPH::ObjectLayer NON_MOVING = 0;
  static constexpr JPH::ObjectLayer MOVING = 1;
  static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

// Tells Jolt which layers can collide with each other
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    switch (a) {
      case Layers::NON_MOVING: return b == Layers::MOVING;
      case Layers::MOVING: return true;
      default: return false;
    }
  }
};

// Broadphase layers (coarse collision detection)
namespace BroadPhaseLayers {
  static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
  static constexpr JPH::BroadPhaseLayer MOVING(1);
  static constexpr JPH::uint NUM_LAYERS(2);
};

class BPLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface {
public:
  BPLayerInterfaceImpl() {
    mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }
  JPH::uint GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    JPH_ASSERT(layer < Layers::NUM_LAYERS);
    return mObjectToBroadPhase[layer];
  }
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    switch ((JPH::BroadPhaseLayer::Type)layer) {
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
      default: return "UNKNOWN";
    }
  }

private:
  JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const override {
    switch (layer) {
      case Layers::NON_MOVING: return bpLayer == BroadPhaseLayers::MOVING;
      case Layers::MOVING: return true;
      default: return false;
    }
  }
};

struct ServerGame {
  shared::ComponentRegistry componentRegistry;
  entt::registry registry;
  std::map<ENetPeer*, entt::entity> peerEntityMap;
  uint32_t nextEntityId = 0;

  // Jolt physics
  JPH::PhysicsSystem physicsSystem;
  BPLayerInterfaceImpl broadPhaseLayerInterface;
  ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
  ObjectLayerPairFilterImpl objectLayerPairFilter;
  //JPH::TempAllocatorImpl tempAllocator{10 * 1024 * 1024}; // 10MB
  JPH::TempAllocatorImpl* tempAllocator = nullptr;
  JPH::JobSystemThreadPool* jobSystem = nullptr;

  ServerGame() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    jobSystem = new JPH::JobSystemThreadPool(
      JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
      std::thread::hardware_concurrency() - 1);
    physicsSystem.Init(
      1024, 0, 1024, 1024,
      broadPhaseLayerInterface,
      objectVsBroadPhaseLayerFilter,
      objectLayerPairFilter
    );
  }

  ~ServerGame() {
    delete tempAllocator;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    delete jobSystem;
  }
};

void input_tick(entt::registry& registry);
void movement_system(entt::registry& registry, float dt);
void render_model_change(entt::registry& registry, float dt);
void hardcoded_spinning_light(entt::registry& registry, float dt,
                              uint32_t lightEntity);
std::tuple<uint32_t, entt::entity> new_entity(ServerGame& g);
void registerServerHandlers(ServerNetwork& network);
JPH::BodyID createPlayerBody(ServerGame& game, float x, float y, float z);
void createFloor(ServerGame& game);

std::vector<uint8_t> serializeEntities(
    entt::registry& registry,
    const shared::ComponentRegistry& componentRegistry,
    shared::PacketType packetType, const std::vector<entt::entity>& entities,
    bool dirtyOnly);
