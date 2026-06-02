#pragma once

namespace shared::summer {

// Summer "escape" minigame layout. World axes: horizontal X/Y, Z is up.
// All values live here so the server logic and the client overlay agree, and
// so designers can tune the survival area, waves, and trigger pad in one place.
//
// Summer overworld play bounds (walkable strip): X [15, 170], Y [-105, -10].
// Trigger pad, survival region, corners, and overlay all use this frame.
struct Layout {
  static constexpr float kWorldMinX = 15.0f;
  static constexpr float kWorldMaxX = 170.0f;
  static constexpr float kWorldMinY = -105.0f;
  static constexpr float kWorldMaxY = -10.0f;
  // Inset from world edges so spawns/regions stay clearly inside the map.
  static constexpr float kInset = 10.0f;

  // Number of shrink waves and per-wave duration (seconds). The red survival
  // region linearly interpolates (by side length) toward the next target over
  // each wave's duration.
  static constexpr int kWaveCount = 3;
  float waveDurationSec[kWaveCount] = {10.0f, 10.0f, 10.0f};
  // Each wave's target side = current side * shrinkFactor (0.5 => 1/4 area).
  float shrinkFactor = 0.5f;
  // Seconds after begin/restart before "outside the region" can fail the run
  // (lets teleported players settle without an instant restart loop).
  float startGraceSec = 0.75f;

  // Initial survival region: nearly the full summer strip (inset from edges).
  float regionMinX = kWorldMinX + kInset;
  float regionMinY = kWorldMinY + kInset;
  float regionMaxX = kWorldMaxX - kInset;
  float regionMaxY = kWorldMaxY - kInset;

  // Overlay (top-down minimap) matches the summer world bounds.
  float mapMinX = kWorldMinX;
  float mapMinY = kWorldMinY;
  float mapMaxX = kWorldMaxX;
  float mapMaxY = kWorldMaxY;

  // Trigger pad: all connected players must stand inside this AABB to start.
  // padCenterZ is the marker's world height (and roughly the ground level the
  // players gather at); the pad test itself only uses XY.
  float padCenterX = 162.350f;
  float padCenterY = -92.372f;
  float padCenterZ = 37.0f;   // trigger marker / pad collision height
  float playFloorZ = 50.0f;  // avatar Z on pad and on run scatter (above landscape)
  float padHalfExtent = 6.0f;

  // Start corners (world XY) on begin/restart — four corners of the play strip.
  // Order: SW, SE, NW, NE (slots 1–4 map to corner index slot-1).
  float cornerX[4] = {kWorldMinX + kInset, kWorldMaxX - kInset,
                      kWorldMinX + kInset, kWorldMaxX - kInset};
  float cornerY[4] = {kWorldMinY + kInset, kWorldMinY + kInset,
                      kWorldMaxY - kInset, kWorldMaxY - kInset};

  [[nodiscard]] static Layout defaults() { return Layout{}; }

  [[nodiscard]] static float clampX(float x) {
    return x < kWorldMinX ? kWorldMinX : (x > kWorldMaxX ? kWorldMaxX : x);
  }
  [[nodiscard]] static float clampY(float y) {
    return y < kWorldMinY ? kWorldMinY : (y > kWorldMaxY ? kWorldMaxY : y);
  }

  [[nodiscard]] bool isInsidePad(float x, float y) const {
    return x >= padCenterX - padHalfExtent && x <= padCenterX + padHalfExtent &&
           y >= padCenterY - padHalfExtent && y <= padCenterY + padHalfExtent;
  }

  [[nodiscard]] static bool isInsideRegion(float x, float y, float minX,
                                           float minY, float maxX, float maxY) {
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
  }
};

}  // namespace shared::summer
