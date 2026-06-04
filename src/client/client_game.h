#pragma once
#include <atomic>
#include <cstdint>
#include <entt/entt.hpp>
#include <map>
#include <mutex>

#include "audio_engine.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "shared/component_registry.h"
#include "shared/protocol.h"
#include "shared/puzzles/maze/layout.h"
#include "shared/puzzles/tangram/arena_layout.h"
#include "shared/puzzles/tangram/defaults.h"
#include "spsc_queue.h"

struct GLFWwindow;
class ClientNetwork;

struct ClientGame {
  shared::ComponentRegistry componentRegistry;

  entt::registry renderRegistry;
  std::map<uint32_t, entt::entity> renderEntityMap;
  uint32_t renderEntityId = UINT32_MAX;

  entt::registry networkRegistry;
  std::map<uint32_t, entt::entity> networkEntityMap;
  uint32_t networkEntityId = UINT32_MAX;

  std::mutex snapshotMutex;
  std::atomic<bool> snapshotDirty = false;
  std::atomic<bool> running = true;

  SpscQueue<shared::InputPacket, 256> inputQueue;
  shared::maze_layout::Config mazeLayout =
      shared::maze_layout::Config::defaults();
  shared::tangram::ArenaLayout tangramArena =
      shared::tangram::ArenaLayout::defaults();
  // Entity id under screen-center reticle (updated each frame during tangram).
  uint32_t tangramCrosshairTargetId = 0;
  // Latest game state from the server (drives the credits screen / music).
  shared::GameStateType currentGameState = shared::GameStateType::OVERWORLD;
  AudioEngine audio;
};

void syncToRender(ClientGame& game);
void bootstrapClientWorldSnapshot(ClientGame& game);
void registerClientHandlers(ClientNetwork& network);
[[nodiscard]] bool isOverworldMazePuzzleActive(const ClientGame& game);
[[nodiscard]] bool isOverworldMazePuzzleComplete(const ClientGame& game);
[[nodiscard]] bool isLocalOverworldMazePuzzleControl(const ClientGame& game);
[[nodiscard]] bool isOverworldTangramPuzzleActive(const ClientGame& game);
[[nodiscard]] uint8_t tangramRoleIsolationStage(const ClientGame& game);
[[nodiscard]] uint8_t localOverworldPlayerSlot(const ClientGame& game);
[[nodiscard]] bool isLocalOverworldTangramPuzzleControl(const ClientGame& game);

// Screen-center reticle pick for tangram rotation (0 = none).
[[nodiscard]] uint32_t pickTangramPieceAtScreenCenter(
    const ClientGame& game, const glm::mat4& view, const glm::mat4& projection);

void processInput(GLFWwindow* window, const ClientGame& game,
                  SpscQueue<shared::InputPacket, 256>& inputQueue,
                  InputKeys& prevKeys, bool debugMode);
void updateWinterMazeWindowTitle(GLFWwindow* window, const ClientGame& game);
void printEntityPositions(const ClientGame& game);
void updateSoundEmitters(ClientGame& game, float listenerX, float listenerY,
                         float listenerZ, float dt);
