#pragma once
#include <cstdint>

namespace shared {
enum class PacketType : uint8_t {
  // input packets from client to server
  INPUT,
  // state update packets from server to client
  UPDATE_ENTITY,
  SPAWN_ENTITY,
  ASSIGN_ENTITY,
  DESPAWN_ENTITY,
  PUZZLE_STATE,
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
  uint8_t keys;
  float mouseDx;
  float mouseDy;
};

struct PuzzleStatePacket {
  PacketType type;
  uint16_t puzzleID = 0;
  uint8_t puzzleStatus = 0;
  uint32_t localPuzzleTimeMs = 0;
};
}  // namespace shared