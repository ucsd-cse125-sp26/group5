#pragma once

// Tangram puzzle: 7 pieces, ghost slots form a swan (head facing +X).
// Slot poses match the reference swan photo (7 colored pieces, no 8th slot).
//
// Piece id → role in swan:
//   1 orange large tri — left wing
//   2 blue large tri   — body base
//   3 pink medium tri  — lower body right
//   4 red small tri    — back / tail
//   5 purple small tri — head / beak
//   6 green square     — chest (diamond)
//   7 yellow para      — neck
//
// Long-term: Rebecca places spring_tangram_slot_1..7 empties on landscape.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "shared/puzzles/tangram/defaults.h"

namespace shared::tangram_puzzle {

inline constexpr int kPieceCount = 7;
inline constexpr float kRotateStepRad = 0.785398163f;  // 45°
inline constexpr int kRotateStepCount = 8;
inline constexpr float kPieceThickness = 0.35f;
inline constexpr float kGhostSlotThickness = 0.06f;

inline constexpr const char* kTargetShapeName = "Memory Swan";
inline constexpr const char* kTargetShapeHint =
    "Push all 7 pieces into the colored ghost swan on the goal board.";

inline constexpr float kShapeGoalHalfX = 2.65f;
inline constexpr float kShapeGoalHalfY = 3.75f;

inline constexpr int kRequiredPlayersForStart = 4;

enum class PieceShape : uint8_t {
  LargeTriangle = 0,
  MediumTriangle = 1,
  SmallTriangle = 2,
  Square = 3,
  Parallelogram = 4,
};

struct PieceDef {
  uint8_t id;
  const char* modelName;
  const char* displayName;
  PieceShape shape;
  float scaleX;
  float scaleY;
  float scaleZ;
  float targetRelX;
  float targetRelY;
  float targetRotRad;
  uint8_t colorR;
  uint8_t colorG;
  uint8_t colorB;
};

// Swan layout — pieces ~12% smaller, centers spread from centroid so adjacent
// slots leave walk/push gaps (~0.45m) while keeping the swan silhouette.
// SAT-verified: no footprint overlap (tangram_slot_validate.h).
inline constexpr PieceDef kPieces[kPieceCount] = {
    {1, "tangram_1", "Orange wing", PieceShape::LargeTriangle, 2.112f, 2.112f,
     kPieceThickness, -2.25f, -0.15f, 2.3561945f, 235, 145, 55},
    {2, "tangram_2", "Blue base", PieceShape::LargeTriangle, 2.112f, 2.112f,
     kPieceThickness, -0.10f, -2.29f, 1.5707963f, 58, 118, 195},
    {3, "tangram_3", "Pink body", PieceShape::MediumTriangle, 1.496f, 1.496f,
     kPieceThickness, 1.26f, -0.73f, 4.71238898f, 235, 130, 155},
    {4, "tangram_4", "Red tail", PieceShape::SmallTriangle, 1.056f, 1.056f,
     kPieceThickness, 1.46f, 0.44f, 3.92699082f, 195, 65, 75},
    {5, "tangram_5", "Purple head", PieceShape::SmallTriangle, 1.056f, 1.056f,
     kPieceThickness, 1.85f, 2.19f, std::numbers::pi_v<float>, 145, 95, 185},
    {6, "tangram_6", "Green chest", PieceShape::Square, 0.924f, 0.924f,
     kPieceThickness, -0.10f, 1.22f, 0.78539816f, 75, 165, 95},
    {7, "tangram_7", "Yellow neck", PieceShape::Parallelogram, 1.144f, 1.672f,
     kPieceThickness, 0.29f, 2.78f, 1.5707963f, 238, 205, 55},
};

[[nodiscard]] inline const PieceDef* pieceDefForId(uint8_t id) {
  for (const PieceDef& p : kPieces) {
    if (p.id == id) return &p;
  }
  return &kPieces[0];
}

[[nodiscard]] inline const char* modelForId(uint8_t id) {
  return pieceDefForId(id)->modelName;
}

inline constexpr const char* kGhostModelNames[kPieceCount] = {
    "tangram_ghost_1", "tangram_ghost_2", "tangram_ghost_3",
    "tangram_ghost_4", "tangram_ghost_5", "tangram_ghost_6",
    "tangram_ghost_7",
};

[[nodiscard]] inline const char* ghostModelForId(uint8_t id) {
  for (int i = 0; i < kPieceCount; ++i) {
    if (kPieces[i].id == id) return kGhostModelNames[i];
  }
  return kGhostModelNames[0];
}

[[nodiscard]] inline glm::vec3 targetWorldPosition(const PieceDef& def,
                                                   float boardCenterX,
                                                   float boardCenterY,
                                                   float platformTopZ) {
  return {boardCenterX + def.targetRelX, boardCenterY + def.targetRelY,
          platformTopZ + kGhostSlotThickness * 0.5f + 0.02f};
}

[[nodiscard]] inline glm::vec3 targetWorldPosition(const PieceDef& def) {
  return targetWorldPosition(def, shared::tangram_defaults::kPuzzleCenterX,
                             shared::tangram_defaults::kPuzzleCenterY,
                             shared::tangram_defaults::kPlatformTopZ);
}

[[nodiscard]] inline glm::quat targetWorldRotation(const PieceDef& def) {
  return glm::angleAxis(def.targetRotRad, glm::vec3(0.0f, 0.0f, 1.0f));
}

[[nodiscard]] inline glm::vec3 slotSnapWorldPosition(const PieceDef& def) {
  return {shared::tangram_defaults::kPuzzleCenterX + def.targetRelX,
          shared::tangram_defaults::kPuzzleCenterY + def.targetRelY,
          shared::tangram_defaults::pieceSurfaceZ(def.scaleZ)};
}

[[nodiscard]] inline bool isInsideShapeGoalZone(float x, float y, float centerX,
                                                float centerY) {
  return x >= centerX - kShapeGoalHalfX && x <= centerX + kShapeGoalHalfX &&
         y >= centerY - kShapeGoalHalfY && y <= centerY + kShapeGoalHalfY;
}

[[nodiscard]] inline float snapRadiusForPiece(const PieceDef& def) {
  const float footprint = std::min(def.scaleX, def.scaleY);
  return std::clamp(footprint * 0.45f, 0.50f, 1.15f);
}

[[nodiscard]] inline float normalizeYawRad(float yaw) {
  constexpr float kPi = std::numbers::pi_v<float>;
  constexpr float kTwoPi = kPi * 2.0f;
  yaw = std::fmod(yaw + kPi, kTwoPi);
  if (yaw < 0.0f) yaw += kTwoPi;
  return yaw - kPi;
}

[[nodiscard]] inline bool yawMatchesTarget(float yaw, float targetRad) {
  return std::abs(normalizeYawRad(yaw - targetRad)) <=
         kRotateStepRad * 0.51f;
}

[[nodiscard]] inline glm::quat quatFromYawRad(float yaw) {
  return glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f));
}

}  // namespace shared::tangram_puzzle
