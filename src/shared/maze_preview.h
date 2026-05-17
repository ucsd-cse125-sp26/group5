#pragma once

namespace shared::maze_preview {

// Overworld mini-maze board (must match server spawn + maze_trigger AABB).
constexpr float kCenterX = 0.0f;
constexpr float kCenterY = 16.0f;
constexpr float kCenterZ = 4.5f;
constexpr float kHalfExtent = 4.0f;

constexpr float kLookAtX = kCenterX;
constexpr float kLookAtY = kCenterY;
constexpr float kLookAtZ = kCenterZ;

[[nodiscard]] inline bool isInsideTriggerRegion(float x, float y) {
  return x >= kCenterX - kHalfExtent && x <= kCenterX + kHalfExtent &&
         y >= kCenterY - kHalfExtent && y <= kCenterY + kHalfExtent;
}

}  // namespace shared::maze_preview
