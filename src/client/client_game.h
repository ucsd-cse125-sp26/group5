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
  uint8_t puzzleStatus = 0; //0 = inactive, 1 = in progress, 2 = solved, 3 = failed
  uint16_t puzzleID = 0;
  uint32_t localPuzzleTimeMs = 0;
};

void registerClientHandlers(ClientNetwork& network);
void processInput(GLFWwindow* window, ClientNetwork& network,
                  uint8_t& prevKeys);
void printEntityPositions(const ClientGame& game);
