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
  SEASON_MUSIC,
  // Server-driven video playback. Append-only: keep these last so existing
  // packet byte values stay stable across separately built server/clients.
  VIDEO_PLAY,
  VIDEO_STOP,
  // Client→server demo debug-panel command. Append-only: keep last.
  DEBUG_COMMAND,
};

// Demo debug-control-panel commands. The active season / target puzzle is
// encoded in DebugCommandPacket::arg as a shared::SectionSeasonMap value
// (WINTER=0, FALL=1, SUMMER=2, SPRING=3); commands that ignore it pass 0.
enum class DebugCommand : uint8_t {
  SET_SEASON,              // arg = SectionSeasonMap
  CYCLE_SEASON,            // arg ignored
  SPAWN_FRAGMENT_CURRENT,  // arg ignored (current active season)
  SPAWN_FRAGMENT_ALL,      // arg ignored (reveal all four fragments)
  START_PUZZLE,            // arg = SectionSeasonMap
  FINISH_PUZZLE,           // arg = SectionSeasonMap (pretend-win: reveal frag)
  PICKUP_FRAGMENT,         // arg = SectionSeasonMap (organic collect+progress)
  TELEPORT_TO_PUZZLE,      // arg = SectionSeasonMap
  TOGGLE_BARRIER_COLLISION,   // arg ignored
  TOGGLE_BARRIER_VISIBILITY,  // arg ignored
  RESET_TO_OVERWORLD_SPAWN,   // arg ignored
  TRIGGER_CREDITS,            // arg ignored
  PRINT_POSITIONS,            // arg ignored
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

// Overworld seasonal loop (client crossfades between tracks).
struct SeasonMusicPacket {
  PacketType type = PacketType::SEASON_MUSIC;
  uint32_t soundId = 0;
  float volume = 0.35f;
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

// Fixed-size POD framed by sizeof+memcpy. uint32 `arg` placed right after the
// type byte (matching VideoPlayPacket's ordering) so the 1+3pad+4 layout is
// identical across MinGW/Linux builds.
struct DebugCommandPacket {
  PacketType type = PacketType::DEBUG_COMMAND;
  DebugCommand cmd = DebugCommand::PRINT_POSITIONS;
  uint8_t _pad[2] = {0, 0};
  uint32_t arg = 0;
};
}  // namespace shared
