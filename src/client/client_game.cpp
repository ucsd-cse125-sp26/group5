#include "client_game.h"

#include <GLFW/glfw3.h>

#include <cassert>
#include <cstring>

#include "client/spsc_queue.h"
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
    if (!meta) {
      offset += dataSize;
      continue;
    }
    meta->deserialize(game.networkRegistry, ent, data + offset, dataSize);
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

          auto entity = game.networkRegistry.create();
          game.networkEntityMap[entityId] = entity;
          game.networkRegistry.emplace<shared::Entity>(entity, entityId);
          deserializeComponents(game, entity, data, offset, len);
        }
      });

  network.dispatcher().on(
      shared::PacketType::ASSIGN_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::AssignPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        game.networkEntityId = pkt.entityId;
      });

  network.dispatcher().on(
      shared::PacketType::DESPAWN_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::DespawnPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        auto it = game.networkEntityMap.find(pkt.entityId);
        if (it != game.networkEntityMap.end()) {
          game.networkRegistry.destroy(it->second);
          game.networkEntityMap.erase(it);
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

          auto it = game.networkEntityMap.find(entityId);
          if (it != game.networkEntityMap.end()) {
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

void syncToRender(ClientGame& game) {
  game.renderEntityId = game.networkEntityId;
  shared::cloneRegistry(game.componentRegistry, game.networkRegistry, game.networkEntityMap, game.renderRegistry, game.renderEntityMap);
}

// ── Input ────────────────────────────────────────────────

void processInput(GLFWwindow* window, SpscQueue<shared::InputPacket, 256>& inputQueue,
                  uint8_t& prevKeys) {
  uint8_t keys = 0;
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) keys |= 0x01;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) keys |= 0x02;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) keys |= 0x04;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) keys |= 0x08;
  if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) keys |= 0x80;
  if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) keys |= 0x40;
  if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) keys |= 0x20;
  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) keys |= 0x10;

  static bool mouseInit = false;
  static double prevMouseX = 0.0, prevMouseY = 0.0;
  float mouseDx = 0.0f, mouseDy = 0.0f;
  bool captured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
  if (captured) {
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    if (mouseInit) {
      mouseDx = static_cast<float>(mouseX - prevMouseX);
      mouseDy = static_cast<float>(mouseY - prevMouseY);
    } else {
      mouseInit = true;
    }
    prevMouseX = mouseX;
    prevMouseY = mouseY;
  } else {
    mouseInit = false;  // re-prime on next capture
  }

  if (keys != prevKeys || mouseDx != 0.0f || mouseDy != 0.0f) {
    shared::InputPacket pkt;
    pkt.type = shared::PacketType::INPUT;
    pkt.keys = keys;
    pkt.mouseDx = mouseDx;
    pkt.mouseDy = mouseDy;
    inputQueue.tryPush(pkt);
  }
  prevKeys = keys;
}

// ── Debug ────────────────────────────────────────────────

void printEntityPositions(const ClientGame& game) {
  auto view = game.renderRegistry.view<shared::Entity, shared::Position>();
  for (auto ent : view) {
    auto& e = view.get<shared::Entity>(ent);
    auto& p = view.get<shared::Position>(ent);
    printf("entity %u @ (%f, %f)%s\n", e.id, p.x, p.y,
           e.id == game.renderEntityId ? " (me)" : "");
  }
}
