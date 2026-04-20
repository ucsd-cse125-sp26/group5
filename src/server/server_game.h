#pragma once
#include <enet/enet.h>

#include <cstdint>
#include <entt/entt.hpp>
#include <map>
#include <vector>

#include "shared/component_registry.h"
#include "shared/protocol.h"

class ServerNetwork;

struct ServerGame {
  shared::ComponentRegistry componentRegistry;
  entt::registry registry;
  std::map<ENetPeer*, entt::entity> peerEntityMap;
  uint32_t nextEntityId = 0;
};

void input_tick(entt::registry& registry);
void movement_system(entt::registry& registry, float dt);
void render_model_change(entt::registry& registry, float dt);
void hardcoded_spinning_light(entt::registry& registry, float dt,
                              uint32_t lightEntity);
std::tuple<uint32_t, entt::entity> new_entity(ServerGame& g);
void registerServerHandlers(ServerNetwork& network);

// Serializes a set of entities and their synced components into a packet
// buffer. packetType controls the header byte (SPAWN_ENTITY or UPDATE_ENTITY).
// dirtyOnly is reserved for future delta updates — currently ignored (always
// full snapshot).
std::vector<uint8_t> serializeEntities(
    entt::registry& registry,
    const shared::ComponentRegistry& componentRegistry,
    shared::PacketType packetType, const std::vector<entt::entity>& entities,
    bool dirtyOnly);
