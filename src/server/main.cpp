#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "game_state.h"
#include "server_game.h"
#include "server_level_loader.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/hello.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/simple_profiler.h"

int main() {
  std::cout << "Hello World Server";
  shared::hello();

  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  ServerNetwork network;
  if (!network.init(7777, 4)) {
    return EXIT_FAILURE;
  }
  game.network = &network;

  registerServerHandlers(network);
  loadLevel(game);
  initWorldEntities(game);

  // Start in the Overworld
  game.gameStateManager.changeState(game,
                                    std::make_unique<OverworldState>());

  network.onConnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("A new client connected from %x:%u.\n", peer->address.host,
           peer->address.port);

    if (g.unused_player_slots.empty()) {
        enet_peer_disconnect(peer, 0);
        return;
    }

    peer->data = (void*)"Client information";
    PlayerAvatars slots = g.unused_player_slots.back();
    g.unused_player_slots.pop_back();
    g.active_players[peer] = slots;

    entt::entity activeEntity = entt::null;
    StateType currentState = g.gameStateManager.currentState()->getStateType();

    if (currentState == StateType::OVERWORLD) {
        activeEntity = slots.overworld_avatar;
    } else if (currentState == StateType::MAZE) {
        activeEntity = slots.maze_avatar;
    }

    // Send full state of all existing entities for the current state to the new client
    std::vector<entt::entity> existing;
    if (currentState == StateType::OVERWORLD) {
        auto view = g.registry.view<shared::OverworldTag>();
        for (auto ent : view) existing.push_back(ent);
    } else if (currentState == StateType::MAZE) {
        auto view = g.registry.view<shared::MazeTag>();
        for (auto ent : view) existing.push_back(ent);
    }

    if (!existing.empty()) {
      auto buf =
          serializeEntities(g.registry, g.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, existing, false);
      net::sendRaw(peer, buf.data(), buf.size());
    }

    // Tell the new client which entity is theirs
    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId = g.registry.get<shared::Entity>(activeEntity).id;
    net::sendPacket(peer, assignPkt);
  };

  network.onDisconnect = [&network](ServerGame& g, ENetPeer* peer) {
    auto it = g.active_players.find(peer);
    if (it == g.active_players.end()) return;

    printf("%s disconnected.\n", (const char*)peer->data);
    PlayerAvatars slots = it->second;

    auto resetSlotEntity = [&g](entt::entity entity) {
      if (g.registry.all_of<shared::PlayerInput>(entity)) {
        auto& input = g.registry.get<shared::PlayerInput>(entity);
        input.keys = 0;
        input.keys_prev = 0;
        input.keys_newly_pressed = 0;
        input.mouseDx = 0.0f;
        input.mouseDy = 0.0f;
      }
      if (g.registry.all_of<shared::Velocity>(entity)) {
        auto& velocity = g.registry.get<shared::Velocity>(entity);
        velocity.dx = 0.0f;
        velocity.dy = 0.0f;
        velocity.dz = 0.0f;
      }
    };
    resetSlotEntity(slots.overworld_avatar);
    resetSlotEntity(slots.maze_avatar);

    g.unused_player_slots.push_back(slots);
    g.active_players.erase(it);
    peer->data = nullptr;
  };

  auto previousTime = std::chrono::high_resolution_clock::now();
  const float fixedDt = 1.0f / 60.0f;
  float accumulator = 0.0f;
  while (true) {
    network.poll(game);

    auto currentTime = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(currentTime - previousTime).count();
    previousTime = currentTime;
    accumulator += dt;

    while (accumulator >= fixedDt) {
      game.gameStateManager.update(game, fixedDt);
      accumulator -= fixedDt;

      SIMPLE_PROFILE_SCOPE("Broadcast State");
      std::vector<entt::entity> allEnts;
      if (game.gameStateManager.currentState() && game.gameStateManager.currentState()->getStateType() == StateType::OVERWORLD) {
          auto view = game.registry.view<shared::OverworldTag>();
          for (auto ent : view) allEnts.push_back(ent);
      } else if (game.gameStateManager.currentState() && game.gameStateManager.currentState()->getStateType() == StateType::MAZE) {
          auto view = game.registry.view<shared::MazeTag>();
          for (auto ent : view) allEnts.push_back(ent);
      }

      if (!allEnts.empty()) {
        auto buf =
            serializeEntities(game.registry, game.componentRegistry,
                              shared::PacketType::UPDATE_ENTITY, allEnts, false);
        net::broadcastRaw(network.getHost(), buf.data(), buf.size());
      }
      SIMPLE_PROFILE_FRAME_END("Server");
      SIMPLE_PROFILE_FRAME_START();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  network.shutdown();
  return EXIT_SUCCESS;
}
