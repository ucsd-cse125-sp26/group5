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
  uint32_t myEntityId = UINT32_MAX;
};

void registerClientHandlers(ClientNetwork& network);
void processInput(GLFWwindow* window, ClientNetwork& network, InputKeys& prevKeys);
void printEntityPositions(const ClientGame& game);
