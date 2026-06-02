#include <gtest/gtest.h>

#include "shared/puzzles/tangram/slot_validate.h"

TEST(TangramLayout, DefaultSwanSlotsDoNotOverlap) {
  EXPECT_FALSE(shared::tangram_slot_validate::defaultCodeLayoutOverlaps());
}

TEST(TangramLayout, IdenticalSlotsOverlap) {
  std::array<shared::tangram_slot::SlotPose, 7> poses{};
  for (auto& slot : poses) {
    slot.relX = 0.0f;
    slot.relY = 0.0f;
    slot.rotRad = 0.0f;
    slot.valid = true;
  }
  EXPECT_TRUE(shared::tangram_slot_validate::slotPosesOverlap(poses));
}
