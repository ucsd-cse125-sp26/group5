#include "client_game.h"

#include <GLFW/glfw3.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "client/spsc_queue.h"
#include "client_network.h"
#include "shared/components.h"
#include "shared/protocol.h"
#include "shared/simple_profiler.h"

namespace {

void removeSyncedComponent(entt::registry& registry, entt::entity entity,
                           shared::ComponentTypeId cid) {
  switch (cid) {
    case shared::CID_POSITION:
      registry.remove<shared::Position>(entity);
      break;
    case shared::CID_ENTITY:
      registry.remove<shared::Entity>(entity);
      break;
    case shared::CID_RENDERINFO:
      registry.remove<shared::RenderInfo>(entity);
      break;
    case shared::CID_CAMERA:
      registry.remove<shared::Camera>(entity);
      break;
    case shared::CID_VELOCITY:
      registry.remove<shared::Velocity>(entity);
      break;
    case shared::CID_POINTLIGHT:
      registry.remove<shared::PointLight>(entity);
      break;
    case shared::CID_SCENE:
      registry.remove<shared::Scene>(entity);
      break;
    case shared::CID_DIRECTIONALLIGHT:
      registry.remove<shared::DirectionalLight>(entity);
      break;
    case shared::CID_OVERWORLD_MAZE_PUZZLE:
      registry.remove<shared::OverworldMazePuzzleState>(entity);
      break;
  }
}

}  // namespace

// ── Component deserialization helper ─────────────────────
//
// Reads (componentTypeId, dataSize, data) tuples from the stream and applies
// them to the given entity. Shared by both SPAWN and UPDATE handlers.

static void deserializeComponents(ClientGame& game, entt::entity ent,
                                  const uint8_t* data, size_t& offset,
                                  size_t len) {
  SIMPLE_PROFILE_SCOPE("Deserialize Components");
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
        printf("CLIENT: spawn %u entities\n", entityCount); 
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
        printf("CLIENT: despawn entity %u\n", pkt.entityId); 
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
            size_t entityStart = offset;
            uint16_t compCount;
            std::memcpy(&compCount, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);

            bool
                present[static_cast<size_t>(shared::CID_OVERWORLD_MAZE_PUZZLE) +
                        1] = {};
            for (uint16_t c = 0; c < compCount; c++) {
              shared::ComponentTypeId cid;
              std::memcpy(&cid, data + offset, sizeof(uint16_t));
              offset += sizeof(uint16_t);
              uint16_t dataSize;
              std::memcpy(&dataSize, data + offset, sizeof(uint16_t));
              offset += sizeof(uint16_t);
              if (cid < std::size(present)) present[cid] = true;
              offset += dataSize;
            }

            auto entity = it->second;
            for (auto cid : game.componentRegistry.syncedIds()) {
              if (cid < std::size(present) && !present[cid]) {
                removeSyncedComponent(game.networkRegistry, entity, cid);
              }
            }

            offset = entityStart;
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
  shared::cloneRegistry(game.componentRegistry, game.networkRegistry,
                        game.networkEntityMap, game.renderRegistry,
                        game.renderEntityMap);
}

// ── Input ────────────────────────────────────────────────

bool isOverworldMazePuzzleActive(const ClientGame& game) {
  auto view = game.renderRegistry.view<shared::OverworldMazePuzzleState>();
  for (auto ent : view) {
    if (view.get<shared::OverworldMazePuzzleState>(ent).active) return true;
  }
  return false;
}

bool isLocalOverworldMazePuzzleControl(const ClientGame& game) {
  if (!isOverworldMazePuzzleActive(game)) return false;

  auto it = game.renderEntityMap.find(game.renderEntityId);
  if (it == game.renderEntityMap.end() ||
      !game.renderRegistry.valid(it->second)) {
    return false;
  }
  if (!game.renderRegistry.all_of<shared::RenderInfo>(it->second)) {
    return false;
  }
  return game.renderRegistry.get<shared::RenderInfo>(it->second).modelName ==
         "cube";
}

void processInput(GLFWwindow* window, const ClientGame& game,
                  SpscQueue<shared::InputPacket, 256>& inputQueue,
                  InputKeys& prevKeys,
                  bool debugMode) {
  InputKeys keys = 0;
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) keys |= KEY_FORWARD;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) keys |= KEY_LEFT;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) keys |= KEY_BACKWARD;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) keys |= KEY_RIGHT;
  if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) keys |= KEY_SWAP_MODEL;
  if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) keys |= KEY_MODEL_SMALLER;
  if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) keys |= KEY_MODEL_BIGGER;
  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) keys |= KEY_JUMP;
  if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) keys |= KEY_LIGHT_DIM;
  if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) keys |= KEY_LIGHT_BRIGHT;
  if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) keys |= KEY_CYCLE_SCENE;
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) keys |= KEY_EXIT_MINIGAME;
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) keys |= KEY_PICKUP;
  if (debugMode) {
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) keys |= KEY_DEBUG_COMPLETE_SECTION;
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) keys |= KEY_DEBUG_TOGGLE_BARRIERS;
  }

  // Overworld preview board: each client controls one direction on the shared
  // green piece (slot 1=up, 2=down, 3=left, 4=right).
  if (isLocalOverworldMazePuzzleControl(game)) {
    auto it = game.renderEntityMap.find(game.renderEntityId);
    uint8_t slot = 1;
    if (it != game.renderEntityMap.end() &&
        game.renderRegistry.valid(it->second) &&
        game.renderRegistry.all_of<shared::RenderInfo>(it->second)) {
      slot = game.renderRegistry.get<shared::RenderInfo>(it->second).playerSlot;
      if (slot < 1 || slot > 4) slot = 1;
    }
    switch (slot) {
      case 1:
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
          keys |= KEY_SPIRIT_UP;
        break;
      case 2:
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
          keys |= KEY_SPIRIT_DOWN;
        break;
      case 3:
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
          keys |= KEY_SPIRIT_LEFT;
        break;
      case 4:
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
          keys |= KEY_SPIRIT_RIGHT;
        break;
      default:
        break;
    }
  }
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
