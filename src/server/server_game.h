#pragma once
#include <enet/enet.h>

#include <cstdint>
#include <entt/entt.hpp>
#include <map>
#include <vector>

#include "shared/component_registry.h"
#include "shared/protocol.h"

class ServerNetwork;

// Server-only component: bitmask of component type ids whose values have
// changed since the last broadcast. Bit `id` corresponds to ComponentTypeId
// `id`. Used by serializeEntities() to skip unchanged entities/components
// when dirtyOnly is true.
//
// Requires ComponentTypeId values to fit in [0, 31]. If we grow past that,
// switch to std::bitset or a larger integer.
struct DirtyTracker {
  uint32_t mask = 0;

  void mark(shared::ComponentTypeId id) { mask |= (1u << id); }
  [[nodiscard]] bool test(shared::ComponentTypeId id) const {
    return (mask >> id) & 1u;
  }
  [[nodiscard]] bool any() const { return mask != 0; }

  void clear() { mask = 0; }
};

struct ServerGame {
  shared::ComponentRegistry componentRegistry;
  entt::registry registry;
  std::map<ENetPeer*, entt::entity> peerEntityMap;
  uint32_t nextEntityId = 0;
};

void movement_system(entt::registry& registry, float dt);
void render_model_change(entt::registry& registry, float dt);
void registerServerHandlers(ServerNetwork& network);

std::vector<uint8_t> serializeEntities(
    entt::registry& registry,
    const shared::ComponentRegistry& componentRegistry,
    shared::PacketType packetType, const std::vector<entt::entity>& entities,
    bool dirtyOnly);
