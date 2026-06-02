#pragma once

#include "shared/puzzles/maze/defaults.h"

namespace shared::maze_layout {

struct Config {
  float spawnBaseX = maze_preview::kSpawnBaseX;
  float spawnBaseY = maze_preview::kSpawnBaseY;
  float spawnHeightZ = maze_preview::kSpawnHeightZ;
  float spawnOffsetX[4] = {
      maze_preview::kSpawnOffsetX[0], maze_preview::kSpawnOffsetX[1],
      maze_preview::kSpawnOffsetX[2], maze_preview::kSpawnOffsetX[3]};
  float spawnOffsetY[4] = {
      maze_preview::kSpawnOffsetY[0], maze_preview::kSpawnOffsetY[1],
      maze_preview::kSpawnOffsetY[2], maze_preview::kSpawnOffsetY[3]};

  float triggerCenterX = maze_preview::kTriggerCenterX;
  float triggerCenterY = maze_preview::kTriggerCenterY;
  float triggerCenterZ = maze_preview::kSpawnHeightZ;
  float halfExtent = maze_preview::kHalfExtent;
  float mazeGap = maze_preview::kMazeGap;

  float boardCenterX = maze_preview::kBoardCenterX;
  float boardCenterY = maze_preview::kBoardCenterY;
  float boardCenterZ = maze_preview::kBoardCenterZ;

  bool autoBoardFromTrigger = true;

  static Config defaults();

  void syncBoardFromTrigger();

  // Keep board readable above terrain without overriding map-authored X/Y.
  void resolveBoardPlacement();

  // Raises preview-board Z only after map load (see kBoardHeightBoostZ).
  void applyHeightBoost(float dz = maze_preview::kBoardHeightBoostZ);

  [[nodiscard]] bool isInsideTrigger(float x, float y) const;

  // Upright wall (X–Z plane) placement helpers.
  [[nodiscard]] float boardWallBaseZ() const;
  [[nodiscard]] float boardFaceY() const;

  [[nodiscard]] float lookAtX() const { return boardCenterX; }
  [[nodiscard]] float lookAtY() const { return boardFaceY(); }
  [[nodiscard]] float lookAtZ() const { return boardWallBaseZ(); }
};

}  // namespace shared::maze_layout
