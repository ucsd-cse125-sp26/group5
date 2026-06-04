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
  // Server-driven video playback. Append-only: keep these last so existing
  // packet byte values stay stable across separately built server/clients.
  VIDEO_PLAY,
  VIDEO_STOP,
};

enum class GameStateType : uint8_t {
  OVERWORLD,
  MAZE,
  CREDITS,
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
  // Network entity id under the screen-center reticle (0 = none).
  uint32_t rotateTargetId = 0;
};

struct SoundEventPacket {
  PacketType type = PacketType::SOUND_EVENT;
  uint32_t soundId;
  float x, y, z;
  float volume = 1.0f;
  float pitch = 1.0f;
  bool positional = true;
};

// Fixed-size POD, framed by sizeof+memcpy like SoundEventPacket. uint32 first
// after the type keeps the layout alignment identical across MinGW/Linux.
struct VideoPlayPacket {
  PacketType type = PacketType::VIDEO_PLAY;
  uint32_t targetEntityId = 0;  // in-world placement; 0 = fullscreen
  uint16_t videoId = 0;         // index into the client's video path table
  uint8_t mode = 0;             // 0 = fullscreen cutscene, 1 = in-world screen
  uint8_t loop = 0;
};

struct VideoStopPacket {
  PacketType type = PacketType::VIDEO_STOP;
};
}  // namespace shared