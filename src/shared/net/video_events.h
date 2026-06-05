#pragma once
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"

// Reusable server-side trigger for the video system. Drop a single call at any
// game event (e.g. a puzzle-completion site) to play a clip on every client.
namespace net {

inline void broadcastVideoPlay(ENetHost* host, uint16_t videoId,
                               uint8_t mode = 0, bool loop = false,
                               uint32_t targetEntityId = 0) {
  shared::VideoPlayPacket pkt;
  pkt.videoId = videoId;
  pkt.mode = mode;
  pkt.loop = loop ? 1 : 0;
  pkt.targetEntityId = targetEntityId;
  broadcastPacket(host, pkt);
}

inline void broadcastVideoStop(ENetHost* host) {
  shared::VideoStopPacket pkt;
  broadcastPacket(host, pkt);
}

}  // namespace net
