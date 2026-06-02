#pragma once

namespace shared::tangram_slot {

struct SlotPose {
  float relX = 0.0f;
  float relY = 0.0f;
  float rotRad = 0.0f;
  bool valid = false;
};

struct Config {
  SlotPose slots[7]{};
  bool anyFromMap = false;
};

}  // namespace shared::tangram_slot
