#pragma once
#include <enet/enet.h>

#include <cstddef>
#include <cstdint>

namespace net {

template <typename T>
void sendPacket(ENetPeer* peer, const T& packet, bool reliable = true, uint8_t channel = 0) {
  uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  ENetPacket* p = enet_packet_create(&packet, sizeof(T), flags);
  enet_peer_send(peer, channel, p);
}

inline void sendRaw(ENetPeer* peer, const uint8_t* data, size_t len, bool reliable = true, uint8_t channel = 0) {
  uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  ENetPacket* p = enet_packet_create(data, len, flags);
  enet_peer_send(peer, channel, p);
}

template <typename T>
void broadcastPacket(ENetHost* host, const T& packet, bool reliable = true, uint8_t channel = 0) {
  uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  ENetPacket* p = enet_packet_create(&packet, sizeof(T), flags);
  enet_host_broadcast(host, channel, p);
}

inline void broadcastRaw(ENetHost* host, const uint8_t* data, size_t len, bool reliable = true, uint8_t channel = 0) {
  uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
  ENetPacket* p = enet_packet_create(data, len, flags);
  enet_host_broadcast(host, channel, p);
}

}  // namespace net
