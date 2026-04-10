#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <map>

struct GLFWwindow;
class ClientNetwork;

struct ClientGame {
  entt::registry registry;
  std::map<uint32_t, entt::entity> entityMap;
  uint32_t myEntityId = UINT32_MAX;
};

void registerClientHandlers(ClientNetwork& network);
void processInput(GLFWwindow* window, ClientNetwork& network,
                  uint8_t& prevKeys);
void printEntityPositions(const ClientGame& game);
