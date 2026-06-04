#pragma once

#include <string>

#include "shared/puzzles/maze/layout.h"
#include "shared/puzzles/tangram/arena_layout.h"
#include "shared/puzzles/tangram/slot_layout.h"

namespace shared {

class ParsedModel;

namespace map_gamelogic_layout {

// World-space AABB around the "Fallen house" landmark. Sized from the node's
// mesh vertices at load (scale is (1,1,1), so extents can't come from scale).
// Used as the end-game gather region: when all players are inside, credits roll.
struct FallenHouseRegion {
  bool valid = false;
  float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
  float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
  [[nodiscard]] bool contains(float x, float y, float z) const {
    return valid && x >= minX && x <= maxX && y >= minY && y <= maxY &&
           z >= minZ && z <= maxZ;
  }
};

struct FallLayout {
  float triggerCenterX = 50.0f;
  float triggerCenterY = -16.0f;
  float triggerCenterZ = 0.0f;
  float playCenterX = 50.0f;
  float playCenterY = 0.0f;
  float playCenterZ = 0.0f;
  float triggerHalfX = 8.0f;
  float triggerHalfY = 8.0f;
  float playHalfX = 12.0f;
  float playHalfY = 12.0f;
  float spawnHeight = 20.0f;
  float markerTopZ() const { return playCenterZ + 0.6f; }
};

// End-game landmark mesh node in the landscape (Blender object name).
inline constexpr const char* kFallenHouseNode = "Fallen house";

inline constexpr const char* kMazeTriggerNode = "maze_trigger";
inline constexpr const char* kMazeBoardNode = "maze_board";
inline constexpr const char* kPlayerStartNode = "player_start";

// Autumn / Fall — falling-game puzzle (auto-read planned).
inline constexpr const char* kFallPlayerStartNode = "fall_player_start";
inline constexpr const char* kFallTriggerNode = "fall_trigger";
inline constexpr const char* kFallPlayZoneNode = "fall_play_zone";
inline constexpr const char* kAutumnTriggerNode = "autumn_trigger";
inline constexpr const char* kAutumnZoneNode = "autumn_zone";

// Spring — tangram push puzzle (auto-read planned).
inline constexpr const char* kSpringPlayerStartNode = "spring_player_start";
inline constexpr const char* kSpringTriggerNode = "spring_trigger";
inline constexpr const char* kSpringTangramZoneNode = "spring_tangram_zone";

// Reads Blender Empty positions from an already-loaded map and updates layout.
// Missing nodes keep their current values (defaults from maze_preview.h).
// Returns true when at least maze_trigger was found.
bool tryApplyMazeLayoutFromMap(const ParsedModel& parsed,
                               maze_layout::Config& layout);

bool tryApplyMazeLayoutFromMapFile(const std::string& path,
                                   maze_layout::Config& layout);

// Spring tangram arena: spring_trigger, spring_tangram_zone, optional
// spring_player_start. Does not affect maze_layout (Winter connect spawn stays
// on player_start).
bool tryApplyTangramArenaFromMap(const ParsedModel& parsed,
                                 tangram::ArenaLayout& layout);

bool tryApplyTangramArenaFromMapFile(const std::string& path,
                                     tangram::ArenaLayout& layout);

// Fall challenge: fall_trigger = gather/start pad, fall_play_zone = cube arena.
// Missing nodes keep the current defaults.
bool tryApplyFallLayoutFromMap(const ParsedModel& parsed, FallLayout& layout);
bool tryApplyFallLayoutFromMapFile(const std::string& path, FallLayout& layout);

// End-game gather region: world-space AABB of the "Fallen house" node, padded.
// Returns false (region.valid stays false) when the node is missing.
bool tryApplyFallenHouseRegionFromMap(const ParsedModel& parsed,
                                      FallenHouseRegion& region);

// Reads spring_tangram_slot_1 .. spring_tangram_slot_7 empties (position +
// yaw). relX/relY are offsets from boardCenter; falls back to code defaults if
// missing.
bool tryApplyTangramSlotsFromMap(const ParsedModel& parsed, float boardCenterX,
                                 float boardCenterY,
                                 tangram_slot::Config& layout);

}  // namespace map_gamelogic_layout
}  // namespace shared
