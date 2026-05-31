#pragma once

namespace shared::maze_preview {

// World axes: horizontal X/Y, Z is up (same as map_loader / default.glb).
//
// Fallback defaults when landscape.glb has no matching Empty nodes.
// At load time map_gamelogic_layout reads maze_trigger, maze_board, and
// player_start from the glb and overrides game.mazeLayout.
//
// Edit below only when testing without a map or when nodes are missing:
//   A) Player connect spawn
//   B) Trigger pad (kHalfExtent still tuned here)
//   C) Board — or place maze_board Empty in Blender

// ── A) Player initialization (connect / overworld enter) ─────────────────
// Only these control where avatars first appear (outside the trigger).
constexpr float kSpawnBaseX = 57.294f;
constexpr float kSpawnBaseY = 12.244f;
constexpr float kSpawnHeightZ = 8.8f;
constexpr float kSpawnOffsetX[4] = {-2.0f, 2.0f, -2.0f, 2.0f};
constexpr float kSpawnOffsetY[4] = {-1.0f, -1.0f, 0.0f, 0.0f};

// ── B) Trigger region (four players must stand inside to start puzzle) ───
// Orange/green pad markers + isInsideTriggerRegion() use this AABB.
// Move X/Y together to relocate pad; keep away from mountain (do not hug
// left/X-min on your map). Increase kHalfExtent for a bigger pad.
constexpr float kTriggerCenterX = 57.294f;
constexpr float kTriggerCenterY = 17.744f;
constexpr float kHalfExtent = 4.0f;
inline constexpr int kRequiredPlayersForStart = 4;

// Code-only lift for the preview board mesh (trigger + spawn unchanged).
inline constexpr float kBoardHeightBoostZ = 1.0f;

// ── C) Upright mini-maze board (X–Z wall, −Y face toward trigger) ─────────
// Default: board center north (+Y) of the trigger pad. Place maze_board Empty
// in Blender; Y must be past triggerCenterY + kHalfExtent + kMazeGap.
constexpr float kMazeGap = 2.0f;
constexpr float kBoardCenterX = kTriggerCenterX;
constexpr float kBoardCenterY =
    kTriggerCenterY + kHalfExtent + kMazeGap + 0.5f;
constexpr float kBoardCenterZ = kSpawnHeightZ;

constexpr float kLookAtX = kBoardCenterX;
constexpr float kLookAtY = kBoardCenterY;
constexpr float kLookAtZ = kBoardCenterZ;

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
