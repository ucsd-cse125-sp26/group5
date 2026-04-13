#include "client_game.h"

#include <GLFW/glfw3.h>

#include <cassert>
#include <cstring>

#include "client_network.h"
#include "shared/components.h"
#include "shared/protocol.h"

// ── Component deserialization helper ─────────────────────
//
// Reads (componentTypeId, dataSize, data) tuples from the stream and applies
// them to the given entity. Shared by both SPAWN and UPDATE handlers.

static void deserializeComponents(ClientGame& game, entt::entity ent,
                                  const uint8_t* data, size_t& offset,
                                  size_t len) {
  assert(offset + sizeof(uint16_t) <= len && "read overflows packet");
  uint16_t compCount;
  std::memcpy(&compCount, data + offset, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  for (uint16_t c = 0; c < compCount; c++) {
    shared::ComponentTypeId cid;
    assert(offset + sizeof(uint16_t) <= len && "read overflows packet");
    std::memcpy(&cid, data + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);
    uint16_t dataSize;
    assert(offset + sizeof(uint16_t) <= len && "read overflows packet");
    std::memcpy(&dataSize, data + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);
    assert(offset + dataSize <= len && "read overflows packet");
    auto* meta = game.componentRegistry.find(cid);
    if (!meta) return;
    meta->deserialize(game.registry, ent, data + offset, dataSize);
    offset += dataSize;
  }
}

// ── Packet handlers ──────────────────────────────────────

void registerClientHandlers(ClientNetwork& network) {
  network.dispatcher().on(
      shared::PacketType::SPAWN_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        size_t offset = sizeof(shared::PacketType);
        uint16_t entityCount;
        std::memcpy(&entityCount, data + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        for (uint16_t i = 0; i < entityCount; i++) {
          uint32_t entityId;
          std::memcpy(&entityId, data + offset, sizeof(uint32_t));
          offset += sizeof(uint32_t);

          auto entity = game.registry.create();
          game.entityMap[entityId] = entity;
          game.registry.emplace<shared::Entity>(entity, entityId);
          deserializeComponents(game, entity, data, offset, len);
        }
      });

  network.dispatcher().on(
      shared::PacketType::ASSIGN_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::AssignPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        game.myEntityId = pkt.entityId;
      });

  network.dispatcher().on(
      shared::PacketType::DESPAWN_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::DespawnPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        auto it = game.entityMap.find(pkt.entityId);
        if (it != game.entityMap.end()) {
          game.registry.destroy(it->second);
          game.entityMap.erase(it);
          printf("Destroyed entity %d\n", pkt.entityId);
        }
      });

  network.dispatcher().on(
      shared::PacketType::UPDATE_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        size_t offset = sizeof(shared::PacketType);
        uint16_t entityCount;
        std::memcpy(&entityCount, data + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        for (uint16_t i = 0; i < entityCount; i++) {
          uint32_t entityId;
          std::memcpy(&entityId, data + offset, sizeof(uint32_t));
          offset += sizeof(uint32_t);

          auto it = game.entityMap.find(entityId);
          if (it != game.entityMap.end()) {
            deserializeComponents(game, it->second, data, offset, len);
          } else {
            // Entity not known — skip its components
            uint16_t compCount;
            std::memcpy(&compCount, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            for (uint16_t c = 0; c < compCount; c++) {
              offset += sizeof(uint16_t);  // componentTypeId
              uint16_t dataSize;
              std::memcpy(&dataSize, data + offset, sizeof(uint16_t));
              offset += sizeof(uint16_t);
              offset += dataSize;
            }
          }
        }
      });
}

// ── Input ────────────────────────────────────────────────

void processInput(GLFWwindow* window, ClientNetwork& network,
                  uint8_t& prevKeys) {
  uint8_t keys = 0;
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) keys |= 0x01;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) keys |= 0x02;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) keys |= 0x04;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) keys |= 0x08;

  if (keys != prevKeys) {
    printf("Sending packet with keys %d\n", keys);
    shared::InputPacket pkt;
    pkt.type = shared::PacketType::KEYBOARD_INPUT;
    pkt.keys = keys;
    network.send(pkt);
  }
  prevKeys = keys;
}

// ── Debug ────────────────────────────────────────────────

void printEntityPositions(const ClientGame& game) {
  auto view = game.registry.view<shared::Entity, shared::Position>();
  for (auto ent : view) {
    auto& e = view.get<shared::Entity>(ent);
    auto& p = view.get<shared::Position>(ent);
    printf("entity %u @ (%f, %f)%s\n", e.id, p.x, p.y,
           e.id == game.myEntityId ? " (me)" : "");
  }
}
