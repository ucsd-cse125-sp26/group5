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
  SOUND_EVENT,
  STATE_CHANGE,
};

enum class GameStateType : uint8_t {
    OVERWORLD,
    MAZE,
};

struct StateChangePacket {
    PacketType type = PacketType::STATE_CHANGE;
    GameStateType state;
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

struct SoundEventPacket {
    PacketType type = PacketType::SOUND_EVENT;
    uint32_t soundId;
    float x, y, z;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool positional = true;
};
}  // namespace shared