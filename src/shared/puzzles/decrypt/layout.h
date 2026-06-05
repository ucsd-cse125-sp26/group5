#pragma once

namespace shared::decrypt {

// Camera / board placement for the end-game cipher board inside the Fallen
// house. Populated from the `decrypt_board` map empty, or derived from the
// Fallen house gather region when the empty is missing.
struct Layout {
  bool valid = false;
  float boardCenterX = 0.0f;
  float boardCenterY = 0.0f;
  float boardCenterZ = 1.5f;
  // Yaw (radians) of the board face normal in the XY plane (+Y = 0).
  float faceYaw = 0.0f;
  float cameraDistance = 5.0f;
  float cameraHeightOffset = 0.5f;

  [[nodiscard]] float lookAtX() const { return boardCenterX; }
  [[nodiscard]] float lookAtY() const { return boardCenterY; }
  [[nodiscard]] float lookAtZ() const { return boardCenterZ; }
};

}  // namespace shared::decrypt
