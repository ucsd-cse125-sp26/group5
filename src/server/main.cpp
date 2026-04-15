#include <chrono>
#include <iostream>
#include <thread>

#include "server_game.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/hello.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"

int main() {
  std::cout << "Hello World Server";
  shared::hello();

  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
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
    auto entity = g.registry.create();
    g.peerEntityMap[peer] = entity;
    g.registry.emplace<shared::Position>(entity, 0.0f, 0.0f);
    g.registry.emplace<shared::Velocity>(entity, 10.0f, 10.0f);
    g.registry.emplace<shared::PlayerInput>(entity, uint8_t(0));
    g.registry.emplace<shared::Entity>(entity, g.nextEntityId);

    // Broadcast the new entity's full state to all clients
    auto buf =
        serializeEntities(g.registry, g.componentRegistry,
                          shared::PacketType::SPAWN_ENTITY, {entity}, false);
    net::broadcastRaw(network.getHost(), buf.data(), buf.size());

    // Tell the new client which entity is theirs
    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId = g.nextEntityId;
    net::sendPacket(peer, assignPkt);

    g.nextEntityId++;
  };

  network.onDisconnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("%s disconnected.\n", (const char*)peer->data);
    auto entity = g.peerEntityMap[peer];

    shared::DespawnPacket despawnPkt;
    despawnPkt.type = shared::PacketType::DESPAWN_ENTITY;
    despawnPkt.entityId = g.registry.get<shared::Entity>(entity).id;
    net::broadcastPacket(network.getHost(), despawnPkt);

    g.registry.destroy(entity);
    g.peerEntityMap.erase(peer);
    peer->data = nullptr;
  };

  registerServerHandlers(network);
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
      movement_system(game.registry, fixedDt);
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
