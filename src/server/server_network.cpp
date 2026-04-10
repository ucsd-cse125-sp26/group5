#include "server_network.h"

#include <cstdio>

bool ServerNetwork::init(uint16_t port, size_t maxClients) {
  if (enet_initialize() != 0) {
    fprintf(stderr, "An error occurred while initializing ENet.\n");
    return false;
  }

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;

  host_ = enet_host_create(&address, maxClients, 2, 0, 0);
  if (host_ == nullptr) {
    fprintf(stderr,
            "An error occurred while trying to create an ENet server host.\n");
    return false;
  }

  return true;
}

void ServerNetwork::shutdown() {
  if (host_) {
    enet_host_destroy(host_);
    host_ = nullptr;
  }
  enet_deinitialize();
}

void ServerNetwork::poll(ServerGame& game) {
  ENetEvent event;
  while (enet_host_service(host_, &event, 0) > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        if (onConnect) onConnect(game, event.peer);
        break;

      case ENET_EVENT_TYPE_RECEIVE:
        dispatcher_.dispatch(game, event.peer, event.packet);
        enet_packet_destroy(event.packet);
        break;

      case ENET_EVENT_TYPE_DISCONNECT:
        if (onDisconnect) onDisconnect(game, event.peer);
        break;

      default:
        break;
    }
  }
}
