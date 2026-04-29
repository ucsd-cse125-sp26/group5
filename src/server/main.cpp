#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include "shared/util.h"
#include "server_game.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/hello.h"
#include "shared/input.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "scene.h"

int main() {
  std::cout << "Hello World Server";
  shared::hello();

  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  
  spawnStaticEntities(game, {
      {5.0f,  5.0f,  0.0f, "cube", 1.0f, "", true},
      {10.0f, 0.0f, -1.0f, "bear", 0.5f,
       (exeDir() / "assets/bear/bear_full.obj").string(), true},
      {0.0f,  0.0f,  -1.0f, "", 1.0f, "", false, 100.0f, 100.0f, 1.0f},
  });
  ServerNetwork network;
  if (!network.init(7777, 4)) {
    return EXIT_FAILURE;
  }

  network.onConnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("A new client connected from %x:%u.\n", peer->address.host,
           peer->address.port);

    // Send full state of all existing entities to the new client
    std::vector<entt::entity> existing;
    auto view = g.registry.view<shared::Entity>();
    for (auto ent : view) existing.push_back(ent);
    if (!existing.empty()) {
      auto buf =
          serializeEntities(g.registry, g.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, existing, false);
      net::sendRaw(peer, buf.data(), buf.size());
    }

    // Create the new player entity
    peer->data = (void*)"Client information";
    auto [entity_id, entity] = new_entity(g);
    g.peerEntityMap[peer] = entity;
    g.registry.emplace<shared::Position>(entity, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    g.registry.emplace<shared::Velocity>(entity, 10.0f, 10.0f);
    g.registry.emplace<shared::RenderInfo>(entity, "cube", 1.0f);
    g.registry.emplace<shared::Camera>(entity, 0.0f, 1.0f);
    g.registry.emplace<shared::PlayerInput>(
        entity, static_cast<InputKeys>(0), static_cast<InputKeys>(0),
        static_cast<InputKeys>(0), 0.0f, 0.0f);
    JPH::BodyID bodyId = g.physics.createPlayerBody(0.0f, 0.0f, 5.0f);
    g.registry.emplace<shared::PhysicsBody>(entity,
                                            bodyId.GetIndexAndSequenceNumber());

    // Broadcast the new entity's full state to all clients
    auto buf =
        serializeEntities(g.registry, g.componentRegistry,
                          shared::PacketType::SPAWN_ENTITY, {entity}, false);
    net::broadcastRaw(network.getHost(), buf.data(), buf.size());

    // Tell the new client which entity is theirs
    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId = entity_id;
    net::sendPacket(peer, assignPkt);
  };

  network.onDisconnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("%s disconnected.\n", static_cast<const char*>(peer->data));
    auto entity = g.peerEntityMap[peer];

    shared::DespawnPacket despawnPkt;
    despawnPkt.type = shared::PacketType::DESPAWN_ENTITY;
    despawnPkt.entityId = g.registry.get<shared::Entity>(entity).id;
    net::broadcastPacket(network.getHost(), despawnPkt);

    auto& pb = g.registry.get<shared::PhysicsBody>(entity);
    g.physics.destroyBody(pb.bodyId);  // clean up Jolt body first
    g.registry.destroy(entity);
    g.peerEntityMap.erase(peer);
    peer->data = nullptr;
  };

  registerServerHandlers(network);
  // Create hardcoded light entity
  auto [light_entity_id, light_entity] = new_entity(game);
  game.registry.emplace<shared::Position>(light_entity, 5.0f, 0.0f, 3.0f, 1.0f,
                                          0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(light_entity, "light_cube", 0.2f);
  // TODO: at some point the point light will be removed from this entity and it
  // will just handle directional
  game.registry.emplace<shared::PointLight>(
      light_entity, 5.0f, 0.0f, 3.0f, 1.0f, 0.09f, 0.032f, 0.1f, 0.1f, 0.1f,
      0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f);
  game.registry.emplace<shared::Scene>(light_entity, "sunny");

  // Create floor entity (large cube, top surface at z=0)
  auto [floor_entity_id, floor_entity] = new_entity(game);
  game.registry.emplace<shared::Position>(floor_entity, 0.0f, 0.0f, -50.5f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(floor_entity, "cube", 100.0f);

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
      input_tick(game.registry);
      movement_system(game, fixedDt);
      render_model_change(game.registry, fixedDt);
      hardcoded_spinning_light(game.registry, fixedDt, light_entity_id);

      // Step Jolt physics
      game.physics.step(fixedDt);
      // printf("Jolt step ok\n");

      // Sync Jolt positions back into ECS
      auto physicsView = game.registry.view<shared::Position, shared::PhysicsBody>();
      for (auto ent : physicsView) {
        auto& pos = physicsView.get<shared::Position>(ent);
        auto& pb = physicsView.get<shared::PhysicsBody>(ent);
        JPH::RVec3 joltPos = game.physics.getBodyInterface().GetPosition(JPH::BodyID(pb.bodyId));
        pos.x = joltPos.GetX();
        pos.y = joltPos.GetY();
        pos.z = joltPos.GetZ();
      }
      scene_cycle_system(game.registry);
      accumulator -= fixedDt;

      // Broadcast delta state to all clients (dirtyOnly=false for now — full
      // snapshot every tick)
      std::vector<entt::entity> allEnts;
      auto view = game.registry.view<shared::Entity>();
      for (auto ent : view) allEnts.push_back(ent);
      auto buf =
          serializeEntities(game.registry, game.componentRegistry,
                            shared::PacketType::UPDATE_ENTITY, allEnts, false);
      net::broadcastRaw(network.getHost(), buf.data(), buf.size());
    }
  }

  network.shutdown();
  return EXIT_SUCCESS;
}
