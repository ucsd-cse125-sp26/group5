#include "client_network.h"

#include <cstdio>

#include "shared/simple_profiler.h"

bool ClientNetwork::connect(const char* host, uint16_t port,
                            uint32_t timeoutMs) {
  if (enet_initialize() != 0) {
    fprintf(stderr, "An error occurred while initializing ENet.\n");
    return false;
  }

  client_ = enet_host_create(nullptr, 1, 2, 0, 0);
  if (client_ == nullptr) {
    fprintf(stderr,
            "An error occurred while trying to create an ENet client host.\n");
    return false;
  }

  ENetAddress address;
  enet_address_set_host(&address, host);
  address.port = port;

  peer_ = enet_host_connect(client_, &address, 2, 0);
  if (peer_ == nullptr) {
    fprintf(stderr, "No available peers for initiating an ENet connection.\n");
    return false;
  }

  ENetEvent event;
  if (enet_host_service(client_, &event, timeoutMs) > 0 &&
      event.type == ENET_EVENT_TYPE_CONNECT) {
    printf("Connection to %s:%u succeeded.\n", host, port);
    return true;
  }

  enet_peer_reset(peer_);
  peer_ = nullptr;
  printf("Connection to %s:%u failed.\n", host, port);
  return false;
}

void ClientNetwork::disconnect() {
  if (!peer_) return;

  enet_peer_disconnect(peer_, 0);

  ENetEvent event;
  bool done = false;
  while (enet_host_service(client_, &event, 3000) > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_RECEIVE:
        enet_packet_destroy(event.packet);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        puts("Disconnection succeeded.");
        done = true;
        break;
      default:
        break;
    }
    if (done) break;
  }
  peer_ = nullptr;
}

void ClientNetwork::shutdown() {
  if (client_) {
    enet_host_destroy(client_);
    client_ = nullptr;
  }
  enet_deinitialize();
}

void ClientNetwork::poll(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Network Poll");
  ENetEvent event;
  while (enet_host_service(client_, &event, 0) > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_RECEIVE:
        dispatcher_.dispatch(game, event.peer, event.packet);
        enet_packet_destroy(event.packet);
        game.snapshotDirty.store(true, std::memory_order_release);
        break;

      case ENET_EVENT_TYPE_DISCONNECT:
        printf("Disconnected from server.\n");
        event.peer->data = nullptr;
        peer_ = nullptr;
        break;

      default:
        break;
    }
  }
}

void ClientNetwork::drainInputQueue(
    SpscQueue<shared::InputPacket, 256>& inputQueue) {
  shared::InputPacket pkt;
  while (inputQueue.tryPop(pkt)) {
    send(pkt);
  }
}