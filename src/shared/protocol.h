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
  TRIGGER_CREDITS,            // arg ignored (force re-roll: resets the latch)
  PRINT_POSITIONS,            // arg ignored
  TOGGLE_DEBUG_LOG,           // arg ignored
  // ── Demo "unstick" controls (append-only). ──────────────────────────────
  // Winter maze: rebind one player's directional power. arg = join slot (1..4),
  // arg2 = MazeDirection (0 = NONE -> grant ALL arrows, 1=UP 2=DOWN 3=LEFT
  // 4=RIGHT).
  SET_MAZE_POWER,
  // Spring tangram: set the role-isolation stage live. arg = stage (0..5; 0 =
  // everyone can do everything).
  SET_TANGRAM_STAGE,
  // Teleport a single player. arg = join slot (1..4), arg2 = destination
  // (0=Winter 1=Fall 2=Summer 3=Spring puzzle, 4 = overworld spawn).
  TELEPORT_PLAYER,
  // Fall challenge difficulty. arg = param id (see DebugFallParam), farg =
  // value.
  SET_FALL_PARAM,
  // Summer escape difficulty. arg = param id (see DebugSummerParam), farg =
  // value.
  SET_SUMMER_PARAM,
  // Spring tangram: grant/revoke one ability for one player, layered on top of
  // the stage rules. arg = join slot (1..4), arg2 = ability (see
  // DebugTangramAbility), farg = enable (>0.5 grant, else revoke).
  SET_TANGRAM_GRANT,
};

// SET_TANGRAM_GRANT::arg2 selector — which per-player ability to grant/revoke.
enum class DebugTangramAbility : uint32_t {
  PUSH = 0,
  ROTATE = 1,
  COLOR = 2,
  SLOTS = 3,
};

// Per-player teleport destinations for TELEPORT_PLAYER::arg2.
enum class DebugTeleportDest : uint32_t {
  WINTER_PUZZLE = 0,
  FALL_PUZZLE = 1,
  SUMMER_PUZZLE = 2,
  SPRING_PUZZLE = 3,
  OVERWORLD_SPAWN = 4,
};

// SET_FALL_PARAM::arg selector. Value carried in DebugCommandPacket::farg.
enum class DebugFallParam : uint32_t {
  FILL_RATE = 0,         // FallChallengeState::fillRate (progress/sec)
  HIT_PENALTY = 1,       // FallChallengeState::hitPenalty (progress lost/hit)
  SPAWN_INTERVAL = 2,    // FallingHazardZone::interval (seconds between drops)
  BURSTS_TO_SWITCH = 3,  // FallingHazardZone::burstsUntilSwitch (int)
};

// SET_SUMMER_PARAM::arg selector. Value carried in DebugCommandPacket::farg.
enum class DebugSummerParam : uint32_t {
  SHRINK_FACTOR = 0,  // summer::Layout::shrinkFactor (lower = harder)
  WAVE_DURATION = 1,  // summer::Layout::waveDurationSec[all] (sec/wave)
  START_GRACE = 2,    // summer::Layout::startGraceSec (settle window)
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
  // Appended after the original 8-byte layout (which stays byte-identical) so
  // newer commands can carry a second integer selector and a float payload.
  uint32_t arg2 = 0;
  float farg = 0.0f;
};
}  // namespace shared
