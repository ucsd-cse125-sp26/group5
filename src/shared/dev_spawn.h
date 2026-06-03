#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

#include "shared/puzzles/maze/layout.h"
#include "shared/puzzles/tangram/arena_layout.h"

namespace shared::dev_spawn {

enum class OverworldSpawn : uint8_t {
  Winter,   // maze player_start (normal gameplay)
  Tangram,  // spring tangram pad (local testing only)
};

// Normal play: Winter. For tangram testing, switch to Tangram and rebuild.
inline constexpr OverworldSpawn kOverworldSpawn = OverworldSpawn::Winter;

// Only active when kOverworldSpawn == Tangram (pieces spawn near ghost slots).
inline constexpr float kTangramDevPieceOffsetFromSlotM = 1.15f;

[[nodiscard]] inline bool spawnTangramPiecesNearSlots() {
  return kOverworldSpawn == OverworldSpawn::Tangram;
}

[[nodiscard]] inline glm::vec3 overworldSpawnPosition(
    OverworldSpawn mode, const maze_layout::Config& maze,
    const tangram::ArenaLayout& tangram, uint8_t joinSlot) {
  const int idx =
      (joinSlot >= 1 && joinSlot <= 4) ? static_cast<int>(joinSlot) - 1 : 0;
  if (mode == OverworldSpawn::Tangram) {
    return {tangram.spawnBaseX + tangram.spawnOffsetX[idx],
            tangram.spawnBaseY + tangram.spawnOffsetY[idx],
            tangram.spawnHeightZ};
  }
  return {maze.spawnBaseX + maze.spawnOffsetX[idx],
          maze.spawnBaseY + maze.spawnOffsetY[idx], maze.spawnHeightZ};
}

[[nodiscard]] inline glm::vec3 overworldSpawnPosition(
    const maze_layout::Config& maze, const tangram::ArenaLayout& tangram,
    uint8_t joinSlot) {
  return overworldSpawnPosition(kOverworldSpawn, maze, tangram, joinSlot);
}

}  // namespace shared::dev_spawn
