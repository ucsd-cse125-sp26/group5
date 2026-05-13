#pragma once
#include <cstdint>

#include "input.h"

namespace shared {
enum class PacketType : uint8_t {
  // input packets from client to server
  INPUT,
  // state update packets from server to client
  UPDATE_ENTITY,
  SPAWN_ENTITY,
  ASSIGN_ENTITY,
  DESPAWN_ENTITY,
};

struct AssignPacket {
  PacketType type;
  uint32_t entityId;
};

struct DespawnPacket {
  PacketType type;
  uint32_t entityId;
};

struct InputPacket {
  PacketType type;
  InputKeys keys;
  float mouseDx;
  float mouseDy;
};
}  // namespace shared
