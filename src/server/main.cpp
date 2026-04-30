#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "game_state.h"
#include "scene.h"
#include "server_game.h"
#include "server_level_loader.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/hello.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/simple_profiler.h"
#include "shared/util.h"

int main() {
  std::cout << "Hello World Server";
  shared::hello();

  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();

  spawnStaticEntities(
      game, {
                {.x = 5.0f,
                 .y = 5.0f,
                 .z = 0.0f,
                 .modelName = "cube",
                 .scale = 1.0f,
                 .meshPath = "",
                 .render = true},
                {.x = 10.0f,
                 .y = 0.0f,
                 .z = -1.0f,
                 .modelName = "bear",
                 .scale = 0.5f,
                 .meshPath = (exeDir() / "assets/bear/bear_full.obj").string(),
                 .render = true},
                {.x = 0.0f,
                 .y = 0.0f,
                 .z = -1.0f,
                 .modelName = "floor",
                 .scale = 1.0f,
                 .meshPath = "",
                 .render = false,
                 .halfX = 100.0f,
                 .halfY = 100.0f,
                 .halfZ = 1.0f},
            });
  ServerNetwork network;
  if (!network.init(7777, 4)) {
    return EXIT_FAILURE;
  }
  game.network = &network;

  registerServerHandlers(network);

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

    auto* currentState = g.gameStateManager.currentState();
    entt::entity activeEntity = currentState->getClientAvatar(slots);
    std::vector<entt::entity> existing = currentState->getStateEntities(g);

    // Send full state of all existing entities
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

    // if we wanted to immediately despawn the player's avatar on disconnect, we could do it here.
    
    // shared::DespawnPacket despawnPkt;
    // despawnPkt.type = shared::PacketType::DESPAWN_ENTITY;

    // // Both slots
    // auto despawnAvatar = [&](entt::entity e) {
    //   if (g.registry.valid(e)) {
    //     despawnPkt.entityId = g.registry.get<shared::Entity>(e).id;
    //     net::broadcastPacket(network.getHost(), despawnPkt);

    //     if (g.registry.all_of<shared::PhysicsBody>(e)) {
    //       auto& pb = g.registry.get<shared::PhysicsBody>(e);
    //       g.physics.destroyBody(pb.bodyId);
    //     }
    //     g.registry.destroy(e);
    //   }
    // };
    // despawnAvatar(slots.overworld_avatar);
    // despawnAvatar(slots.maze_avatar);

    slots.resetControls(g.registry);
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
      
      // Step Jolt physics
      game.physics.step(fixedDt);

      // Sync Jolt positions back into ECS
      auto physicsView =
          game.registry.view<shared::Position, shared::PhysicsBody>();
      for (auto ent : physicsView) {
        auto& pos = physicsView.get<shared::Position>(ent);
        auto& pb = physicsView.get<shared::PhysicsBody>(ent);
        JPH::RVec3 joltPos =
            game.physics.getBodyInterface().GetPosition(JPH::BodyID(pb.bodyId));
        pos.x = joltPos.GetX();
        pos.y = joltPos.GetY();
        pos.z = joltPos.GetZ();
      }
      scene_cycle_system(game.registry);
      accumulator -= fixedDt;

      SIMPLE_PROFILE_SCOPE("Broadcast State");
      std::vector<entt::entity> allEnts = game.gameStateManager.currentState()->getStateEntities(game);
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
