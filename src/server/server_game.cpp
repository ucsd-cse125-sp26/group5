#include "server_game.h"

#include <cstring>

#include "server_network.h"
#include "shared/component_registry.h"
#include "shared/components.h"

// ── Movement system ──────────────────────────────────────

void movement_system(entt::registry& registry, float dt) {
  auto view =
      registry.view<shared::Position, shared::Velocity, shared::PlayerInput>();
  for (auto entity : view) {
    auto& position = view.get<shared::Position>(entity);
    auto& velocity = view.get<shared::Velocity>(entity);
    auto& input = view.get<shared::PlayerInput>(entity);
    velocity.dx = 0;
    velocity.dy = 0;
    if (input.keys & 0x01) velocity.dy = 10.0f;
    if (input.keys & 0x02) velocity.dx = -10.0f;
    if (input.keys & 0x04) velocity.dy = -10.0f;
    if (input.keys & 0x08) velocity.dx = 10.0f;
    position.x += velocity.dx * dt;
    position.y += velocity.dy * dt;
  }
}

// ── Packet handlers ──────────────────────────────────────

void registerServerHandlers(ServerNetwork& network) {
  network.dispatcher().on(
      shared::PacketType::KEYBOARD_INPUT,
      [](ServerGame& game, ENetPeer* sender, const uint8_t* data, size_t len) {
        shared::InputPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        auto it = game.peerEntityMap.find(sender);
        if (it == game.peerEntityMap.end()) return;
        game.registry.replace<shared::PlayerInput>(it->second, pkt.keys);
      });
}

// ── Entity serialization ─────────────────────────────────
//
// Wire format (shared by SPAWN_ENTITY and UPDATE_ENTITY):
//
// ┌─────────────────────────────────────────────────────────┐
// │ PacketType (1 byte)  │  entityCount (2 bytes)           │
// ├─────────────────────────────────────────────────────────┤
// │ entityId (4 bytes)   │  componentCount (2 bytes)        │
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │
// │   └─ ...                                               │
// ├─────────────────────────────────────────────────────────┤
// │ ...more entities...                                     │
// └─────────────────────────────────────────────────────────┘

std::vector<uint8_t> serializeEntities(
    entt::registry& registry, shared::PacketType packetType,
    const std::vector<entt::entity>& entities, bool dirtyOnly) {
  std::vector<uint8_t> buffer;

  size_t headerPos = buffer.size();
  buffer.resize(buffer.size() + sizeof(shared::PacketType) + sizeof(uint16_t));

  uint16_t entityCount = 0;
  for (auto ent : entities) {
    if (!registry.valid(ent) || !registry.all_of<shared::Entity>(ent)) continue;

    uint32_t entityId = registry.get<shared::Entity>(ent).id;
    size_t entityHeaderPos = buffer.size();
    buffer.resize(buffer.size() + sizeof(uint32_t) + sizeof(uint16_t));

    uint16_t compCount = 0;
    for (auto cid : shared::getSyncedComponentIds()) {
      // dirtyOnly filtering reserved for future delta updates
      (void)dirtyOnly;

      auto& meta = shared::getComponentRegistry()[cid];
      size_t before = buffer.size();
      buffer.resize(buffer.size() + sizeof(uint16_t) + sizeof(uint16_t));
      std::vector<uint8_t> compBuf;
      if (meta.serialize(registry, ent, compBuf)) {
        uint16_t dataSize = static_cast<uint16_t>(compBuf.size());
        std::memcpy(&buffer[before], &cid, sizeof(uint16_t));
        std::memcpy(&buffer[before + sizeof(uint16_t)], &dataSize,
                    sizeof(uint16_t));
        buffer.insert(buffer.end(), compBuf.begin(), compBuf.end());
        compCount++;
      } else {
        buffer.resize(before);
      }
    }

    std::memcpy(&buffer[entityHeaderPos], &entityId, sizeof(uint32_t));
    std::memcpy(&buffer[entityHeaderPos + sizeof(uint32_t)], &compCount,
                sizeof(uint16_t));
    entityCount++;
  }

  std::memcpy(&buffer[headerPos], &packetType, sizeof(shared::PacketType));
  std::memcpy(&buffer[headerPos + sizeof(shared::PacketType)], &entityCount,
              sizeof(uint16_t));

  return buffer;
}
