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
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>

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

inline constexpr float kShapeGoalHalfX = 4.0f;
inline constexpr float kShapeGoalHalfY = 5.6f;

inline constexpr int kRequiredPlayersForStart = 4;

// Footprint vertex distance thresholds (meters).
inline constexpr float kSnapFootprintMismatchMax = 0.58f;
inline constexpr float kWinFootprintMismatchMax = 0.38f;

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

// Swan layout — larger pieces with extra spacing between slot centers.
// SAT-verified: no footprint overlap (tangram_slot_validate.h).
inline constexpr PieceDef kPieces[kPieceCount] = {
    {1, "tangram_1", "Orange wing", PieceShape::LargeTriangle, 2.58f, 2.58f,
     kPieceThickness, -2.70f, -0.18f, 2.3561945f, 235, 145, 55},
    {2, "tangram_2", "Blue base", PieceShape::LargeTriangle, 2.58f, 2.58f,
     kPieceThickness, -0.12f, -2.75f, 1.5707963f, 58, 118, 195},
    {3, "tangram_3", "Pink body", PieceShape::MediumTriangle, 1.82f, 1.82f,
     kPieceThickness, 1.51f, -0.88f, 4.71238898f, 235, 130, 155},
    {4, "tangram_4", "Red tail", PieceShape::SmallTriangle, 1.29f, 1.29f,
     kPieceThickness, 1.75f, 0.53f, 3.92699082f, 195, 65, 75},
    {5, "tangram_5", "Purple head", PieceShape::SmallTriangle, 1.29f, 1.29f,
     kPieceThickness, 2.22f, 2.63f, std::numbers::pi_v<float>, 145, 95, 185},
    {6, "tangram_6", "Green chest", PieceShape::Square, 1.13f, 1.13f,
     kPieceThickness, -0.12f, 1.46f, 0.78539816f, 75, 165, 95},
    {7, "tangram_7", "Yellow neck", PieceShape::Parallelogram, 1.40f, 2.04f,
     kPieceThickness, 0.35f, 3.34f, 1.5707963f, 238, 205, 55},
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
    "tangram_ghost_1", "tangram_ghost_2", "tangram_ghost_3", "tangram_ghost_4",
    "tangram_ghost_5", "tangram_ghost_6", "tangram_ghost_7",
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
  return std::clamp(footprint * 0.55f, 0.75f, 1.25f);
}

[[nodiscard]] inline float normalizeYawRad(float yaw) {
  constexpr float kPi = std::numbers::pi_v<float>;
  constexpr float kTwoPi = kPi * 2.0f;
  yaw = std::fmod(yaw + kPi, kTwoPi);
  if (yaw < 0.0f) yaw += kTwoPi;
  return yaw - kPi;
}

// R key steps by kRotateStepRad; win checks use the same 45° grid.
[[nodiscard]] inline float quantizeYawToRotateStep(float yaw) {
  return normalizeYawRad(std::round(yaw / kRotateStepRad) * kRotateStepRad);
}

[[nodiscard]] inline bool yawMatchesTarget(float yaw, float targetRad) {
  return std::abs(normalizeYawRad(quantizeYawToRotateStep(yaw) -
                                  quantizeYawToRotateStep(targetRad))) <= 1e-3f;
}

[[nodiscard]] inline glm::quat quatFromYawRad(float yaw) {
  return glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f));
}

}  // namespace shared::tangram_puzzle
