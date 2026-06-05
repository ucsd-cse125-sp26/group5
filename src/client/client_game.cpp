#include "client_game.h"

#include <GLFW/glfw3.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "client/decrypt_ui.h"
#include "client/spsc_queue.h"
#include "client_network.h"
#include "glm/glm.hpp"
#include "shared/components.h"
#include "shared/protocol.h"
#include "shared/puzzles/tangram/roles.h"
#include "shared/simple_profiler.h"
#include "shared/sound_constants.h"

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
    case shared::CID_COLORBOUNDINGBOX:
      registry.remove<shared::ColorBoundingBox>(entity);
      break;
    case shared::CID_ANIMATIONSTATE:
      registry.remove<shared::AnimationState>(entity);
      break;
    case shared::CID_MAZESPIRITGRID:
      registry.remove<shared::MazeSpiritGrid>(entity);
      break;
    case shared::CID_SOUNDEMITTER:
      registry.remove<shared::SoundEmitter>(entity);
      break;
    case shared::CID_OVERWORLD_TANGRAM_PUZZLE:
      registry.remove<shared::OverworldTangramPuzzleState>(entity);
      break;
    case shared::CID_TANGRAM_PIECE:
      registry.remove<shared::TangramPiece>(entity);
      break;
    case shared::CID_FALL_CHALLENGE:
      registry.remove<shared::FallChallengeState>(entity);
      break;
    case shared::CID_SUMMER_ESCAPE:
      registry.remove<shared::SummerEscapeState>(entity);
      break;
    case shared::CID_DECRYPT_PUZZLE:
      registry.remove<shared::DecryptPuzzleState>(entity);
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
        game.snapshotDirty.store(true, std::memory_order_release);
      });

  network.dispatcher().on(
      shared::PacketType::DESPAWN_ENTITY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::DespawnPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        printf("CLIENT: despawn entity %u\n", pkt.entityId);
        auto it = game.networkEntityMap.find(pkt.entityId);
        if (it != game.networkEntityMap.end()) {
          game.audio.stopAllForEntity(pkt.entityId);
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

            bool present[static_cast<size_t>(shared::CID_SUMMER_ESCAPE) + 1] =
                {};
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

  network.dispatcher().on(
      shared::PacketType::SOUND_EVENT,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::SoundEventPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        if (pkt.positional) {
          game.audio.playSound(pkt.soundId, pkt.x, pkt.y, pkt.z, pkt.volume,
                               pkt.pitch);
        } else {
          game.audio.playNonPositionalSound(pkt.soundId, pkt.volume, pkt.pitch);
        }
      });

  network.dispatcher().on(
      shared::PacketType::STATE_CHANGE,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::StateChangePacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        game.currentGameState = pkt.state;
        game.audio.stopAllGlobalLoops();
        if (pkt.state == shared::GameStateType::OVERWORLD) {
          game.audio.playGlobalLoop(
              static_cast<uint32_t>(shared::SoundId::OVERWORLD_MUSIC), 0.3f);
        } else if (pkt.state == shared::GameStateType::MAZE) {
          game.audio.playGlobalLoop(
              static_cast<uint32_t>(shared::SoundId::MAZE_MUSIC), 0.3f);
        } else if (pkt.state == shared::GameStateType::DECRYPT) {
          game.decryptWrongAnswer.store(false, std::memory_order_release);
          decrypt_ui::onDecryptActivated();
        } else if (pkt.state == shared::GameStateType::CREDITS) {
          game.audio.playGlobalLoop(
              static_cast<uint32_t>(shared::SoundId::CREDITS_MUSIC), 0.3f);
        }
      });

  network.dispatcher().on(
      shared::PacketType::DECRYPT_RESULT,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        if (len < sizeof(shared::DecryptResultPacket)) return;
        shared::DecryptResultPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        if (!pkt.accepted) {
          game.decryptWrongAnswer.store(true, std::memory_order_release);
        }
      });

  // Runs on the network thread — GL is invalid here, so only enqueue a request
  // for the render thread (main.cpp) to act on.
  network.dispatcher().on(
      shared::PacketType::VIDEO_PLAY,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        shared::VideoPlayPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        game.videoQueue.tryPush(VideoRequest{pkt.videoId, pkt.mode, pkt.loop,
                                             pkt.targetEntityId,
                                             /*stop=*/false});
      });

  network.dispatcher().on(
      shared::PacketType::VIDEO_STOP,
      [](ClientGame& game, ENetPeer*, const uint8_t* data, size_t len) {
        game.videoQueue.tryPush(VideoRequest{0, 0, 0, 0, /*stop=*/true});
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
  // Freeze the avatar while the credits roll or during the decrypt puzzle.
  if (game.currentGameState == shared::GameStateType::CREDITS ||
      game.currentGameState == shared::GameStateType::DECRYPT) {
    if (prevKeys != 0) {
      shared::InputPacket pkt;
      pkt.type = shared::PacketType::INPUT;
      pkt.keys = 0;
      pkt.mouseDx = 0.0f;
      pkt.mouseDy = 0.0f;
      pkt.rotateTargetId = 0;
      inputQueue.tryPush(pkt);
    }
    prevKeys = 0;
    return;
  }

  InputKeys keys = 0;
  const bool mazeBoardControl = isLocalOverworldMazePuzzleControl(game);

  if (!mazeBoardControl) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) keys |= KEY_FORWARD;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) keys |= KEY_LEFT;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) keys |= KEY_BACKWARD;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) keys |= KEY_RIGHT;
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) keys |= KEY_SWAP_MODEL;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) keys |= KEY_JUMP;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) keys |= KEY_EXIT_MINIGAME;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) keys |= KEY_PICKUP;
    // Gameplay debug shortcuts (B/N/G/V/Y/F) were removed in favor of the demo
    // debug control panel (Ctrl+Shift+\); those actions now travel over the
    // DEBUG_COMMAND packet instead of the input bitfield.
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

// Sound
void updateSoundEmitters(ClientGame& game, float listenerX, float listenerY,
                         float listenerZ, float dt) {
  SIMPLE_PROFILE_SCOPE("Sound Emitters");
  auto view =
      game.renderRegistry
          .view<shared::Entity, shared::Position, shared::SoundEmitter>();
  for (auto ent : view) {
    auto& entity = view.get<shared::Entity>(ent);
    auto& pos = view.get<shared::Position>(ent);
    auto& emitter = view.get<shared::SoundEmitter>(ent);

    game.audio.updateEmitter(entity.id, emitter, pos.x, pos.y, pos.z, listenerX,
                             listenerY, listenerZ, dt);
  }
}
