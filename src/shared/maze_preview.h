#pragma once

namespace shared::maze_preview {

// Four-player staging pad on the overworld floor (horizontal x,y; z is up).
constexpr float kTriggerCenterX = 0.0f;
constexpr float kTriggerCenterY = 10.0f;
constexpr float kHalfExtent = 4.0f;

// Gap between the north edge of the trigger pad and the maze board (world y).
constexpr float kMazeGap = 2.0f;

// Maze board just north of the trigger, facing south toward players.
constexpr float kBoardCenterX = kTriggerCenterX;
constexpr float kBoardCenterY =
    kTriggerCenterY + kHalfExtent + kMazeGap + 0.5f;
constexpr float kBoardCenterZ = 3.8f;

// Camera / avatar facing target (center of the board).
constexpr float kLookAtX = kBoardCenterX;
constexpr float kLookAtY = kBoardCenterY;
constexpr float kLookAtZ = kBoardCenterZ;

// Board spawn uses these (same as kBoardCenter*).
constexpr float kCenterX = kBoardCenterX;
constexpr float kCenterY = kBoardCenterY;
constexpr float kCenterZ = kBoardCenterZ;

[[nodiscard]] inline bool isInsideTriggerRegion(float x, float y) {
  return x >= kTriggerCenterX - kHalfExtent &&
         x <= kTriggerCenterX + kHalfExtent &&
         y >= kTriggerCenterY - kHalfExtent &&
         y <= kTriggerCenterY + kHalfExtent;
}

}  // namespace shared::maze_preview
