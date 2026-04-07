#include <iostream>

#include "shared/hello.h"
#include "shared/protocol.h"
#include <enet/enet.h>
#include "shared/components.h"
#include <map>
#include <entt/entt.hpp>
#include <thread>
#include <chrono>

// channel 0 is for reliable packets
// channel 1 is for unreliable packets

std::map<ENetPeer*, entt::entity> PeerEntityMap;

void movement_system(entt::registry &registry, float dt) {
  auto view = registry.view<shared::Position, shared::Velocity, shared::PlayerInput>();
  for (auto entity: view) {
    auto &position = view.get<shared::Position>(entity);
    auto &velocity = view.get<shared::Velocity>(entity);
    auto &input = view.get<shared::PlayerInput>(entity);
    velocity.dx = 0;
    velocity.dy = 0;
    if (input.keys & 0x01) {
      velocity.dy = 10.0f;
    }
    if (input.keys & 0x02) {
      velocity.dx = -10.0f;
    }
    if (input.keys & 0x04) {
      velocity.dy = -10.0f;
    }
    if (input.keys & 0x08) {
      velocity.dx = 10.0f;
    }
    position.x += velocity.dx * dt;
    position.y += velocity.dy * dt;
  }
}
int main() {
  std::cout << "Hello World Server";
  shared::hello();
  if (enet_initialize () != 0)
    {
        fprintf (stderr, "An error occurred while initializing ENet.\n");
        return EXIT_FAILURE;
    }
    atexit (enet_deinitialize);

    ENetAddress address;
    ENetHost * server;
    
    /* Bind the server to the default localhost.     */
    /* A specific host address can be specified by   */
    /* enet_address_set_host (& address, "x.x.x.x"); */
    
    address.host = ENET_HOST_ANY;
    /* Bind the server to port 7777. */
    address.port = 7777;
    
    server = enet_host_create (& address /* the address to bind the server host to */, 
                                4      /* allow up to 32 clients and/or outgoing connections */,
                                  2      /* allow up to 2 channels to be used, 0 and 1 */,
                                  0      /* assume any amount of incoming bandwidth */,
                                  0      /* assume any amount of outgoing bandwidth */);
    if (server == NULL)
    {
        fprintf (stderr, 
                "An error occurred while trying to create an ENet server host.\n");
        exit (EXIT_FAILURE);
    }
    ENetEvent event;
    entt::registry registry;
    uint8_t entityCount = 0;

    while (true) {
  
        while (enet_host_service (server, & event, 0) > 0)
        {
            switch (event.type)
            {
              case ENET_EVENT_TYPE_CONNECT: {
                printf ("A new client connected from %x:%u.\n", 
                  event.peer -> address.host,
                  event.peer -> address.port);
                
                // send all entities to the new client
                auto view = registry.view<shared::Entity, shared::Position>();
                for (auto entity: view) {
                  auto entityId = view.get<shared::Entity>(entity).id;
                  auto position = view.get<shared::Position>(entity);
                  shared::SpawnPacket spawnPacket;
                  spawnPacket.type = shared::PacketType::SPAWN_ENTITY;
                  spawnPacket.entityId = entityId;
                  spawnPacket.x = position.x;
                  spawnPacket.y = position.y;
                  ENetPacket * packet = enet_packet_create (&spawnPacket, sizeof(shared::SpawnPacket), ENET_PACKET_FLAG_RELIABLE);
                  enet_peer_send (event.peer, 0, packet);
                }

                /* Store any relevant client information here. */
                event.peer -> data = (void*) "Client information";
                auto entity = registry.create();
                PeerEntityMap[event.peer] = entity;
                registry.emplace<shared::Position>(entity, 0.0f, 0.0f);
                registry.emplace<shared::Velocity>(entity, 10.0f, 10.0f);
                registry.emplace<shared::PlayerInput>(entity, uint8_t(0));
                registry.emplace<shared::Entity>(entity, entityCount);

                // send the new entity to all clients
                shared::SpawnPacket spawnPacket;
                spawnPacket.type = shared::PacketType::SPAWN_ENTITY;
                spawnPacket.entityId = entityCount;
                spawnPacket.x = 0.0f;
                spawnPacket.y = 0.0f;
                ENetPacket * spawnPacketEnet = enet_packet_create (&spawnPacket, sizeof(shared::SpawnPacket), ENET_PACKET_FLAG_RELIABLE);
                enet_host_broadcast(server, 0, spawnPacketEnet);

                // tell the new client their entity 
                shared::AssignPacket assignPacket;
                assignPacket.type = shared::PacketType::ASSIGN_ENTITY;
                assignPacket.entityId = entityCount;
                ENetPacket * AssignPacketEnet = enet_packet_create (&assignPacket, sizeof(shared::AssignPacket), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send (event.peer, 0, AssignPacketEnet);
                entityCount++;
                break;
              }
              case ENET_EVENT_TYPE_RECEIVE: {
                auto type = static_cast<shared::PacketType>(event.packet->data[0]);
                switch (type) {
                    case shared::PacketType::KEYBOARD_INPUT: {

                      shared::InputPacket inputPacket;
                      std::memcpy(&inputPacket, event.packet->data, sizeof(shared::InputPacket));

                      printf("peer %x input: W=%d A=%d S=%d D=%d\n",
                      event.peer->address.host,
                      (inputPacket.keys & 0x01) != 0,
                      (inputPacket.keys & 0x02) != 0,
                      (inputPacket.keys & 0x04) != 0,
                      (inputPacket.keys & 0x08) != 0);
                      

                      auto entity = PeerEntityMap[event.peer];
                      registry.replace<shared::PlayerInput>(entity, inputPacket.keys);
                      break;
                    }
                    default:
                      printf("Received unknown packet type %d\n", type);
                      break;
                }
                enet_packet_destroy (event.packet);
                break;
              }
              
              case ENET_EVENT_TYPE_DISCONNECT: {
                  printf ("%s disconnected.\n", event.peer -> data);
                  auto entity = PeerEntityMap[event.peer];
                  shared::DespawnPacket despawnPacket;
                  despawnPacket.type = shared::PacketType::DESPAWN_ENTITY;
                  despawnPacket.entityId = registry.get<shared::Entity>(entity).id;
                  ENetPacket * DespawnPacketEnet = enet_packet_create (&despawnPacket, sizeof(shared::DespawnPacket), ENET_PACKET_FLAG_RELIABLE);
                  enet_host_broadcast(server, 0, DespawnPacketEnet);
                  registry.destroy(entity);
                  PeerEntityMap.erase(event.peer);
                  /* Reset the peer's client information. */
          
                  event.peer -> data = NULL;
                  break;
                }
              default:
                  break;

            }
        }

        float dt = 0.05f;
        movement_system(registry, dt);

        auto view = registry.view<shared::Position>();
        std::vector<shared::StateEntry> stateEntries;

        for (auto entity: view) {
          auto &position = view.get<shared::Position>(entity);
          auto entityId = registry.get<shared::Entity>(entity).id;
          stateEntries.push_back(shared::StateEntry{entityId, position.x, position.y});
        }
        
        shared::StateHeader stateHeader;
        stateHeader.type = shared::PacketType::UPDATE_POSITION;
        stateHeader.count = stateEntries.size();
        size_t totalSize = sizeof(shared::StateHeader) + sizeof(shared::StateEntry) * stateEntries.size();
        ENetPacket * packet = enet_packet_create (&stateHeader, totalSize, ENET_PACKET_FLAG_RELIABLE);

        std::memcpy(packet->data, &stateHeader, sizeof(shared::StateHeader));
        std::memcpy(packet->data + sizeof(shared::StateHeader), stateEntries.data(), sizeof(shared::StateEntry) * stateEntries.size());
        enet_host_broadcast(server, 0, packet);
        // sleep for 50ms
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    }
    enet_host_destroy(server);
    return EXIT_SUCCESS;


}
