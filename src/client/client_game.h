#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <map>

#include "shared/component_registry.h"

struct GLFWwindow;
class ClientNetwork;

struct ClientGame {
  shared::ComponentRegistry componentRegistry;
  entt::registry registry;
  std::map<uint32_t, entt::entity> entityMap;

  entt::registry renderRegistry;
  std::map<uint32_t, entt::entity> renderEntityMap;
  uint32_t renderEntityId = UINT32_MAX;

  entt::registry networkRegistry;
  std::map<uint32_t, entt::entity> networkEntityMap;
  uint32_t networkEntityId = UINT32_MAX;
  uint32_t myEntityId = UINT32_MAX;
};

void syncToRender(ClientGame& game);
void registerClientHandlers(ClientNetwork& network);
void processInput(GLFWwindow* window, ClientNetwork& network,
                  uint8_t& prevKeys);
void printEntityPositions(const ClientGame& game);
