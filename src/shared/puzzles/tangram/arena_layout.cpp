#include "shared/puzzles/tangram/arena_layout.h"

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

}  // namespace shared::tangram
