#pragma once
#include <atomic>
#include <cstdint>
#include <entt/entt.hpp>
#include <map>
#include <mutex>

#include "audio_engine.h"
#include "shared/component_registry.h"
#include "shared/protocol.h"
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

  AudioEngine audio;
};

void syncToRender(ClientGame& game);
void registerClientHandlers(ClientNetwork& network);
[[nodiscard]] bool isOverworldMazePuzzleActive(const ClientGame& game);
[[nodiscard]] bool isLocalOverworldMazePuzzleControl(const ClientGame& game);

void processInput(GLFWwindow* window, const ClientGame& game,
                  SpscQueue<shared::InputPacket, 256>& inputQueue,
                  InputKeys& prevKeys, bool debugMode);
void printEntityPositions(const ClientGame& game);
void updateSoundEmitters(ClientGame& game, float listenerX, float listenerY,
                         float listenerZ, float dt);
