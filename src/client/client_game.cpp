#include "client_game.h"

#include <GLFW/glfw3.h>

#include <cassert>
#include <cstring>

#include <cmath>

#include "glm/glm.hpp"
#include "client/spsc_queue.h"
#include "client_network.h"
#include "shared/components.h"
#include "shared/protocol.h"
#include "shared/puzzles/tangram/roles.h"
#include "shared/simple_profiler.h"

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
        game.snapshotDirty.store(true, std::memory_order_release);
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
  shared::cloneRegistry(game.componentRegistry, game.networkRegistry,
                        game.networkEntityMap, game.renderRegistry,
                        game.renderEntityMap);
}

void bootstrapClientWorldSnapshot(ClientGame& game) {
  std::scoped_lock lock(game.snapshotMutex);
  syncToRender(game);
  game.snapshotDirty.store(false, std::memory_order_release);
}

// ── Input ────────────────────────────────────────────────

bool isOverworldMazePuzzleActive(const ClientGame& game) {
  auto view = game.renderRegistry.view<shared::OverworldMazePuzzleState>();
  for (auto ent : view) {
    if (view.get<shared::OverworldMazePuzzleState>(ent).active) return true;
  }
  return false;
}

bool isOverworldMazePuzzleComplete(const ClientGame& game) {
  auto view = game.renderRegistry.view<shared::OverworldMazePuzzleState>();
  for (auto ent : view) {
    if (view.get<shared::OverworldMazePuzzleState>(ent).completed) return true;
  }
  return false;
}

void updateWinterMazeWindowTitle(GLFWwindow* window, const ClientGame& game) {
  if (isOverworldMazePuzzleComplete(game)) {
    glfwSetWindowTitle(window, "Winter Maze Complete");
  } else if (isOverworldMazePuzzleActive(game)) {
    glfwSetWindowTitle(window, "Winter Maze - Arrow Control");
  } else {
    glfwSetWindowTitle(window, "Hello World");
  }
}

bool isOverworldTangramPuzzleActive(const ClientGame& game) {
  auto view = game.renderRegistry.view<shared::OverworldTangramPuzzleState>();
  for (auto ent : view) {
    if (view.get<shared::OverworldTangramPuzzleState>(ent).active) return true;
  }
  return false;
}

uint8_t tangramRoleIsolationStage(const ClientGame& game) {
  auto view = game.renderRegistry.view<shared::OverworldTangramPuzzleState>();
  for (auto ent : view) {
    const auto& st = view.get<shared::OverworldTangramPuzzleState>(ent);
    if (st.active) return st.roleIsolationStage;
  }
  return 0;
}

uint8_t localOverworldPlayerSlot(const ClientGame& game) {
  auto it = game.renderEntityMap.find(game.renderEntityId);
  if (it == game.renderEntityMap.end() ||
      !game.renderRegistry.valid(it->second)) {
    return 0;
  }
  if (!game.renderRegistry.all_of<shared::RenderInfo>(it->second)) return 0;
  const uint8_t slot =
      game.renderRegistry.get<shared::RenderInfo>(it->second).playerSlot;
  if (slot < 1 || slot > 4) return 0;
  return slot;
}

bool isLocalOverworldTangramPuzzleControl(const ClientGame& game) {
  return isOverworldTangramPuzzleActive(game);
}

bool isLocalOverworldMazePuzzleControl(const ClientGame& game) {
  if (!isOverworldMazePuzzleActive(game)) return false;

  auto it = game.renderEntityMap.find(game.renderEntityId);
  return it != game.renderEntityMap.end() &&
         game.renderRegistry.valid(it->second);
}

uint32_t pickTangramPieceAtScreenCenter(const ClientGame& game,
                                        const glm::mat4& view,
                                        const glm::mat4& projection) {
  uint32_t bestId = 0;
  float bestDist = 1e9f;
  constexpr float kMaxNdcRadius = 0.14f;

  auto pieceView =
      game.renderRegistry
          .view<shared::Entity, shared::Position, shared::TangramPiece>();
  for (auto ent : pieceView) {
    const auto& pos = pieceView.get<shared::Position>(ent);
    const auto& entity = pieceView.get<shared::Entity>(ent);
    const glm::vec4 clip =
        projection * view * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
    if (clip.w <= 0.0f) continue;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) continue;
    const float d = std::hypot(ndc.x, ndc.y);
    if (d > kMaxNdcRadius || d >= bestDist) continue;
    bestDist = d;
    bestId = entity.id;
  }
  return bestId;
}

void processInput(GLFWwindow* window, const ClientGame& game,
                  SpscQueue<shared::InputPacket, 256>& inputQueue,
                  InputKeys& prevKeys) {
  InputKeys keys = 0;
  const bool mazeBoardControl = isLocalOverworldMazePuzzleControl(game);

  if (!mazeBoardControl) {
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
  }

  if (mazeBoardControl) {
    // Send every arrow key pressed; server MazePadBinding picks the one
    // assigned to this player's join slot.
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) keys |= KEY_SPIRIT_UP;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
      keys |= KEY_SPIRIT_DOWN;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
      keys |= KEY_SPIRIT_LEFT;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
      keys |= KEY_SPIRIT_RIGHT;
  }

  if (isOverworldTangramPuzzleActive(game) &&
      glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
    const uint8_t stage = tangramRoleIsolationStage(game);
    const uint8_t slot = localOverworldPlayerSlot(game);
    if (shared::tangram_roles::canRotate(stage, slot)) {
      keys |= KEY_ROTATE_PIECE;
    }
  }

  // Always allow exiting a minigame (works in maze arrow-control too).
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) keys |= KEY_EXIT_MINIGAME;
  if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) keys |= KEY_DEBUG_PRINT_POS;

  static bool mouseInit = false;
  static double prevMouseX = 0.0, prevMouseY = 0.0;
  static uint32_t prevRotateTargetId = 0;
  float mouseDx = 0.0f, mouseDy = 0.0f;
  const bool tangramActive = isOverworldTangramPuzzleActive(game);
  const uint32_t rotateTargetId =
      tangramActive ? game.tangramCrosshairTargetId : 0;
  const bool lockCamera = mazeBoardControl;
  bool captured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
  if (captured && !lockCamera) {
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

  if (keys != prevKeys || mouseDx != 0.0f || mouseDy != 0.0f ||
      rotateTargetId != prevRotateTargetId ||
      (tangramActive && (keys & KEY_ROTATE_PIECE))) {
    shared::InputPacket pkt;
    pkt.type = shared::PacketType::INPUT;
    pkt.keys = keys;
    pkt.mouseDx = mouseDx;
    pkt.mouseDy = mouseDy;
    pkt.rotateTargetId = rotateTargetId;
    inputQueue.tryPush(pkt);
  }
  prevKeys = keys;
  prevRotateTargetId = rotateTargetId;
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
