#pragma once

#include "shared/puzzles/tangram/defaults.h"

namespace shared::tangram {

struct ArenaLayout {
  float spawnBaseX = tangram_defaults::kSpawnBaseX;
  float spawnBaseY = tangram_defaults::kSpawnBaseY;
  float spawnHeightZ = tangram_defaults::kSpawnHeightZ;
  float spawnOffsetX[4] = {
      tangram_defaults::kSpawnOffsetX[0], tangram_defaults::kSpawnOffsetX[1],
      tangram_defaults::kSpawnOffsetX[2], tangram_defaults::kSpawnOffsetX[3]};
  float spawnOffsetY[4] = {
      tangram_defaults::kSpawnOffsetY[0], tangram_defaults::kSpawnOffsetY[1],
      tangram_defaults::kSpawnOffsetY[2], tangram_defaults::kSpawnOffsetY[3]};

  float triggerCenterX = tangram_defaults::kTriggerCenterX;
  float triggerCenterY = tangram_defaults::kTriggerCenterY;
  float halfExtent = tangram_defaults::kHalfExtent;

  float boardCenterX = tangram_defaults::kPuzzleCenterX;
  float boardCenterY = tangram_defaults::kPuzzleCenterY;
  float boardCenterZ = tangram_defaults::kSpawnHeightZ;

  float platformCenterX = tangram_defaults::kPlatformCenterX;
  float platformCenterY = tangram_defaults::kPlatformCenterY;
  float platformCenterZ = tangram_defaults::kPlatformCenterZ;
  float platformScaleX = tangram_defaults::kPlatformScaleX;
  float platformScaleY = tangram_defaults::kPlatformScaleY;
  float platformScaleZ = tangram_defaults::kPlatformScaleZ;

  static ArenaLayout defaults();

  void syncBoardFromTrigger();

  [[nodiscard]] bool isInsideTrigger(float x, float y) const;

  [[nodiscard]] float platformTopZ() const {
    return platformCenterZ + platformScaleZ * 0.5f;
  }

  [[nodiscard]] float pieceRestZ(float pieceScaleZ) const {
    return platformTopZ() + pieceScaleZ * 0.5f + 0.05f;
  }

  [[nodiscard]] float lookAtX() const { return boardCenterX; }
  [[nodiscard]] float lookAtY() const { return boardCenterY; }
  [[nodiscard]] float lookAtZ() const { return boardCenterZ; }

  // Raises platform, board, and pad spawn by kArenaHeightBoostZ (call once
  // after map load).
  void applyHeightBoost(float dz = tangram_defaults::kArenaHeightBoostZ) {
    platformCenterZ += dz;
    boardCenterZ += dz;
    spawnHeightZ += dz;
  }
};

}  // namespace shared::tangram
