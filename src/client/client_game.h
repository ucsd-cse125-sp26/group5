#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <map>

#include "shared/component_registry.h"
#include "spsc_queue.h"
#include "shared/protocol.h"

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
};

void syncToRender(ClientGame& game);
void registerClientHandlers(ClientNetwork& network);
void processInput(GLFWwindow* window, SpscQueue<shared::InputPacket, 256>& inputQueue,
                  uint8_t& prevKeys);
void printEntityPositions(const ClientGame& game);
