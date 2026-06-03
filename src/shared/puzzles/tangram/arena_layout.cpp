#include "shared/puzzles/tangram/arena_layout.h"

#include <algorithm>

#include "shared/puzzles/tangram/puzzle_data.h"

namespace shared::tangram {

ArenaLayout ArenaLayout::defaults() { return ArenaLayout{}; }

void ArenaLayout::syncBoardFromTrigger() {
  boardCenterX = triggerCenterX;
  boardCenterY = triggerCenterY + halfExtent + 2.0f;
  boardCenterZ = platformTopZ();
}

bool ArenaLayout::isInsideTrigger(float x, float y) const {
  return x >= triggerCenterX - halfExtent && x <= triggerCenterX + halfExtent &&
         y >= triggerCenterY - halfExtent && y <= triggerCenterY + halfExtent;
}

ColorRestoreAabb ArenaLayout::alwaysColorAabb() const {
  constexpr float kPad = 3.0f;
  constexpr float kZPad = 8.0f;

  float minX = platformCenterX - platformScaleX * 0.5f - kPad;
  float maxX = platformCenterX + platformScaleX * 0.5f + kPad;
  float minY = platformCenterY - platformScaleY * 0.5f - kPad;
  float maxY = platformCenterY + platformScaleY * 0.5f + kPad;

  minX = std::min(minX, triggerCenterX - halfExtent - kPad);
  maxX = std::max(maxX, triggerCenterX + halfExtent + kPad);
  minY = std::min(minY, triggerCenterY - halfExtent - kPad);
  maxY = std::max(maxY, triggerCenterY + halfExtent + kPad);

  minX = std::min(minX, boardCenterX - tangram_puzzle::kShapeGoalHalfX - kPad);
  maxX = std::max(maxX, boardCenterX + tangram_puzzle::kShapeGoalHalfX + kPad);
  minY = std::min(minY, boardCenterY - tangram_puzzle::kShapeGoalHalfY - kPad);
  maxY = std::max(maxY, boardCenterY + tangram_puzzle::kShapeGoalHalfY + kPad);

  return ColorRestoreAabb{
      minX, minY, platformCenterZ - platformScaleZ * 0.5f - kZPad,
      maxX, maxY, platformTopZ() + kZPad + 4.0f,
  };
}

}  // namespace shared::tangram
