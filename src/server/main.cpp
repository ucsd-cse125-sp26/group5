#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
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
  initServerGame(game);

  ServerNetwork network;
  if (!network.init(7777, 4)) {
    return EXIT_FAILURE;
  }
  game.network = &network;

  registerServerHandlers(network);

  loadLevel(game);
  initWorldEntities(game);

  // Start in the Overworld
  game.gameStateManager.changeState(game, std::make_unique<OverworldState>());

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

    const uint8_t slot = g.nextPlayerJoinSlot++;
    for (entt::entity av : {slots.overworld_avatar, slots.maze_avatar}) {
      if (g.registry.valid(av) && g.registry.all_of<shared::RenderInfo>(av)) {
        g.registry.get<shared::RenderInfo>(av).playerSlot = slot;
      }
    }
    printf("[Server] Client assigned player slot %" PRIu8 "\n", slot);

    auto* currentState = g.gameStateManager.currentState();
    entt::entity activeEntity = currentState->getClientAvatar(slots);
    std::vector<entt::entity> existing = currentState->getStateEntities(g);
    auto miniEnts = g.miniGameManager.getActiveEntities(g);
    existing.insert(existing.end(), miniEnts.begin(), miniEnts.end());

    if (!existing.empty()) {
      auto buf =
          serializeEntities(g.registry, g.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, existing, false);
      net::sendRaw(peer, buf.data(), buf.size());
    }

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

    // if we wanted to immediately despawn the player's avatar on disconnect, we
    // could do it here.

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

    for (entt::entity av : {slots.overworld_avatar, slots.maze_avatar}) {
      if (g.registry.valid(av) && g.registry.all_of<shared::RenderInfo>(av)) {
        g.registry.get<shared::RenderInfo>(av).playerSlot = 0;
      }
    }
    slots.resetControls(g.registry);
    if (auto* currentState = g.gameStateManager.currentState()) {
      entt::entity activeEntity = currentState->getClientAvatar(slots);
      if (g.registry.valid(activeEntity) &&
          g.registry.all_of<shared::Entity>(activeEntity)) {
        g.miniGameManager.removePlayer(
            g.registry.get<shared::Entity>(activeEntity).id);
      }
    }
    g.unused_player_slots.push_back(slots);
    g.active_players.erase(it);
    if (g.active_players.empty()) {
      g.nextPlayerJoinSlot = 1;
    }
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
      game.miniGameManager.update(game, fixedDt);

      game.physics.step(fixedDt);

      // Jolt → ECS sync. Skip rotation for player entities; their yaw is
      // movement_system's responsibility (Jolt rotation DOFs are locked).
      auto& bi = game.physics.getBodyInterface();
      auto physicsView =
          game.registry.view<shared::Position, shared::PhysicsBody>();
      for (auto ent : physicsView) {
        auto& pos = physicsView.get<shared::Position>(ent);
        auto& pb = physicsView.get<shared::PhysicsBody>(ent);
        JPH::BodyID id(pb.bodyId);
        if (!bi.IsAdded(id)) continue;

        JPH::RVec3 jp = bi.GetPosition(id);
        pos.x = jp.GetX();
        pos.y = jp.GetY();
        pos.z = jp.GetZ();

        if (game.registry.all_of<shared::MazeSpiritGrid>(ent)) {
          constexpr float kMin = 0.5f;
          constexpr float kMax = 13.5f;
          bool bounced = false;
          if (pos.x < kMin) {
            pos.x = kMin;
            bounced = true;
          } else if (pos.x > kMax) {
            pos.x = kMax;
            bounced = true;
          }
          if (pos.y < kMin) {
            pos.y = kMin;
            bounced = true;
          } else if (pos.y > kMax) {
            pos.y = kMax;
            bounced = true;
          }
          if (bounced) {
            bi.SetPosition(id, JPH::RVec3(pos.x, pos.y, pos.z),
                           JPH::EActivation::Activate);
            JPH::Vec3 v = bi.GetLinearVelocity(id);
            bi.SetLinearVelocity(id, JPH::Vec3(0.0f, 0.0f, v.GetZ()));
          }
          auto& grid = game.registry.get<shared::MazeSpiritGrid>(ent);
          const int gxCell = static_cast<int>(std::lround(pos.x * 0.5f));
          const int gyCell = static_cast<int>(std::lround(pos.y * 0.5f));
          grid.gx = static_cast<int8_t>(std::clamp(gxCell, 0, 7));
          grid.gy = static_cast<int8_t>(std::clamp(gyCell, 0, 7));
        }
        if (!game.registry.all_of<shared::PlayerInput>(ent)) {
          JPH::Quat jr = bi.GetRotation(id);
          pos.qw = jr.GetW();
          pos.qx = jr.GetX();
          pos.qy = jr.GetY();
          pos.qz = jr.GetZ();
        }
      }
      accumulator -= fixedDt;

      SIMPLE_PROFILE_SCOPE("Broadcast State");
      std::vector<entt::entity> allEnts =
          game.gameStateManager.currentState()->getStateEntities(game);
      auto miniEnts = game.miniGameManager.getActiveEntities(game);
      allEnts.insert(allEnts.end(), miniEnts.begin(), miniEnts.end());
      if (!allEnts.empty()) {
        auto buf = serializeEntities(game.registry, game.componentRegistry,
                                     shared::PacketType::UPDATE_ENTITY, allEnts,
                                     false);
        net::broadcastRaw(network.getHost(), buf.data(), buf.size());
      }
      SIMPLE_PROFILE_FRAME_END("Server");
      SIMPLE_PROFILE_FRAME_START();
    }

    // Yield so the loop doesn't spin-lock at 100% CPU.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  network.shutdown();
  return EXIT_SUCCESS;
}
