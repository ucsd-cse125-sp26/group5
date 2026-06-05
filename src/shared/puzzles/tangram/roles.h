#pragma once

#include <cstdint>

// Tangram co-op role isolation (incremental rollout).
//
// Set kIsolationStage to enable stages cumulatively:
//   0 — off (everyone pushes, sees color + slots, rotates)
//   1 — infra only (sync stage to clients; no gameplay change)
//   2 — slot 4: move + rotate (no push); slots 1–3: move + push (no rotate)
//   3 — slot 3: move + see ghost slots (no push); slots 1–2 push; slot 4 rotate
//   4 — slot 2: move + see color (no push); only slot 1 pushes (+ prior rules)
//   5 — full split (same push/color/slots/rotate as stage 4; reserved)
//
// All players always keep WASD movement on the platform.

namespace shared::tangram_roles {

inline constexpr uint8_t kStageOff = 0;
inline constexpr uint8_t kStageInfra = 1;
inline constexpr uint8_t kStageRotateP4 = 2;
inline constexpr uint8_t kStageSlotsP3 = 3;
inline constexpr uint8_t kStageColorP2 = 4;
inline constexpr uint8_t kStagePushP1 = 5;

// Full 4-player co-op split (push / color / slots / rotate).
inline constexpr uint8_t kIsolationStage = kStagePushP1;

inline constexpr uint8_t kPushSlot = 1;
inline constexpr uint8_t kColorSlot = 2;
inline constexpr uint8_t kSlotsSlot = 3;
inline constexpr uint8_t kRotateSlot = 4;

[[nodiscard]] inline constexpr bool rolesActive(uint8_t stage) {
  return stage > kStageOff;
}

[[nodiscard]] inline constexpr bool rotateRestricted(uint8_t stage) {
  return stage >= kStageRotateP4;
}

[[nodiscard]] inline constexpr bool slotsRestricted(uint8_t stage) {
  return stage >= kStageSlotsP3;
}

[[nodiscard]] inline constexpr bool colorRestricted(uint8_t stage) {
  return stage >= kStageColorP2;
}

[[nodiscard]] inline constexpr bool pushRestricted(uint8_t stage) {
  return stage >= kStagePushP1;
}

// Physics push split starts at stage 2 (P4 no push; P1–3 push).
[[nodiscard]] inline constexpr bool pushCollisionIsolation(uint8_t stage) {
  return stage >= kStageRotateP4;
}

[[nodiscard]] inline bool canRotate(uint8_t stage, uint8_t playerSlot) {
  if (!rotateRestricted(stage)) return true;
  return playerSlot == kRotateSlot;
}

[[nodiscard]] inline bool canSeeSlots(uint8_t stage, uint8_t playerSlot) {
  if (!slotsRestricted(stage)) return true;
  return playerSlot == kSlotsSlot;
}

[[nodiscard]] inline bool canSeeColor(uint8_t stage, uint8_t playerSlot) {
  if (!colorRestricted(stage)) return true;
  return playerSlot == kColorSlot;
}

[[nodiscard]] inline bool canPush(uint8_t stage, uint8_t playerSlot) {
  if (!pushCollisionIsolation(stage)) return true;
  if (stage >= kStageColorP2) return playerSlot == kPushSlot;
  if (stage >= kStageSlotsP3) {
    return playerSlot == kPushSlot || playerSlot == kColorSlot;
  }
  if (stage >= kStageRotateP4) return playerSlot != kRotateSlot;
  return true;
}

// Per-player grant overlay (see OverworldTangramPuzzleState::grant*). A set bit
// (slot-1) hands that ability to the player on top of whatever the stage
// allows.
[[nodiscard]] inline constexpr bool slotGranted(uint8_t grantMask,
                                                uint8_t playerSlot) {
  return playerSlot >= 1 && playerSlot <= 4 &&
         (grantMask & (1u << (playerSlot - 1))) != 0;
}

[[nodiscard]] inline bool canRotate(uint8_t stage, uint8_t playerSlot,
                                    uint8_t grant) {
  return slotGranted(grant, playerSlot) || canRotate(stage, playerSlot);
}

[[nodiscard]] inline bool canSeeSlots(uint8_t stage, uint8_t playerSlot,
                                      uint8_t grant) {
  return slotGranted(grant, playerSlot) || canSeeSlots(stage, playerSlot);
}

[[nodiscard]] inline bool canSeeColor(uint8_t stage, uint8_t playerSlot,
                                      uint8_t grant) {
  return slotGranted(grant, playerSlot) || canSeeColor(stage, playerSlot);
}

[[nodiscard]] inline bool canPush(uint8_t stage, uint8_t playerSlot,
                                  uint8_t grant) {
  return slotGranted(grant, playerSlot) || canPush(stage, playerSlot);
}

}  // namespace shared::tangram_roles
