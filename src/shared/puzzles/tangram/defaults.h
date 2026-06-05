#pragma once

#include <cstdint>

namespace shared::tangram_defaults {

// Fallback arena layout when landscape.glb has no spring_* empties.
// Map spring_trigger / spring_tangram_zone override these at load time.

// Extra Z added to the tangram board after reading the map (meters). Tweak here
// if the green pad clips into landscape_z before Rebecca moves the Empty.
inline constexpr float kArenaHeightBoostZ = 10.0f;

constexpr float kPlatformScaleZ = 0.6f;
constexpr float kPlatformCenterX = 0.0f;
constexpr float kPlatformCenterY = 0.0f;
constexpr float kPlatformScaleX = 38.0f;
constexpr float kPlatformScaleY = 38.0f;

constexpr float kEstimatedMountainPeakZ = 45.0f;
constexpr float kClearanceAbovePeak = 45.0f;
constexpr float kPlatformCenterZ =
    kEstimatedMountainPeakZ + kClearanceAbovePeak + kPlatformScaleZ * 0.5f;
constexpr float kPlatformTopZ = kPlatformCenterZ + kPlatformScaleZ * 0.5f;

constexpr float kSpawnBaseX = 0.0f;
constexpr float kSpawnBaseY = -16.0f;
constexpr float kSpawnHeightZ = kPlatformTopZ + 0.5f;
constexpr float kSpawnOffsetX[4] = {-3.0f, 3.0f, -3.0f, 3.0f};
constexpr float kSpawnOffsetY[4] = {-1.5f, -1.5f, 0.0f, 0.0f};

constexpr float kTriggerCenterX = 0.0f;
constexpr float kTriggerCenterY = 0.0f;
// Square trigger pad matches full platform (syncTriggerFromPlatform).
constexpr float kHalfExtent = kPlatformScaleX * 0.5f;

constexpr float kPuzzleCenterX = 0.0f;
constexpr float kPuzzleCenterY = 7.0f;
constexpr float kPuzzlePlayHalfSize = 15.0f;
constexpr float kSolvedSquareHalf = 4.5f;

[[nodiscard]] inline float pieceSurfaceZ(float pieceScaleZ) {
  return kPlatformTopZ + pieceScaleZ * 0.5f + 0.05f;
}

}  // namespace shared::tangram_defaults
