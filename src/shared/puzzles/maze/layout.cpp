#include "shared/puzzles/maze/layout.h"

#include <algorithm>
#include <cmath>

namespace shared::maze_layout {
namespace {

constexpr float kBoardZLift = 2.0f;
constexpr float kFaceTowardTriggerY = 0.12f;
constexpr float kBoardNorthPadding = 0.5f;

}  // namespace

Config Config::defaults() { return Config{}; }

void Config::syncBoardFromTrigger() {
  boardCenterX = triggerCenterX;
  boardCenterY = triggerCenterY + halfExtent + mazeGap + kBoardNorthPadding;
  boardCenterZ = triggerCenterZ;
}

void Config::resolveBoardPlacement() {
  // Preserve map-authored X/Y so designers can place board freely.
  boardCenterZ = std::max(boardCenterZ, triggerCenterZ);
}

void Config::applyHeightBoost(float dz) { boardCenterZ += dz; }

float Config::boardWallBaseZ() const { return boardCenterZ + kBoardZLift; }

float Config::boardFaceY() const {
  // Always place the visible face on the side that looks toward trigger.
  return boardCenterY + (triggerCenterY >= boardCenterY ? kFaceTowardTriggerY
                                                        : -kFaceTowardTriggerY);
}

bool Config::isInsideTrigger(float x, float y) const {
  return x >= triggerCenterX - halfExtent && x <= triggerCenterX + halfExtent &&
         y >= triggerCenterY - halfExtent && y <= triggerCenterY + halfExtent;
}

}  // namespace shared::maze_layout
