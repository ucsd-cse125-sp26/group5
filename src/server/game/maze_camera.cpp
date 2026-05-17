#include "server/game/maze_camera.h"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "server/server_game.h"
#include "shared/components.h"
#include "shared/maze_preview.h"

namespace maze_camera {
namespace {

void setYawTowardTarget(shared::Position& position, float targetX,
                        float targetY) {
  const float dx = targetX - position.x;
  const float dy = targetY - position.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) return;

  // Match movement_system / client computeCamera yaw convention.
  const float yaw = std::atan2(-dx / len, dy / len);
  const float half = yaw * 0.5f;
  const float sinHalf = std::sin(half);
  const float cosHalf = std::cos(half);
  position.qw = cosHalf;
  position.qx = 0.0f;
  position.qy = 0.0f;
  position.qz = sinHalf;
}

float yawErrorRad(const shared::Position& position, float targetX,
                  float targetY) {
  const float dx = targetX - position.x;
  const float dy = targetY - position.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) return 0.0f;

  const float desiredYaw = std::atan2(-dx / len, dy / len);
  glm::quat playerRot(position.qw, position.qx, position.qy, position.qz);
  glm::vec3 flat = playerRot * glm::vec3(0.0f, 1.0f, 0.0f);
  flat.z = 0.0f;
  const float flatLen = glm::length(flat);
  if (flatLen < 1e-4f) return 3.14159265f;
  flat /= flatLen;
  const float actualYaw = std::atan2(-flat.x, flat.y);

  float err = desiredYaw - actualYaw;
  while (err > 3.14159265f) err -= 6.28318531f;
  while (err < -3.14159265f) err += 6.28318531f;
  return std::abs(err);
}

}  // namespace

void snapOverworldAvatarsFaceMazePreview(ServerGame& game) {
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      continue;
    }
    auto& position =
        game.registry.get<shared::Position>(slots.overworld_avatar);
    setYawTowardTarget(position, shared::maze_preview::kLookAtX,
                       shared::maze_preview::kLookAtY);

    if (game.registry.all_of<shared::PhysicsBody>(slots.overworld_avatar)) {
      auto& pb = game.registry.get<shared::PhysicsBody>(slots.overworld_avatar);
      auto& bodyInterface = game.physics.getBodyInterface();
      JPH::BodyID bodyId(pb.bodyId);
      if (bodyInterface.IsAdded(bodyId)) {
        glm::quat q(position.qw, position.qx, position.qy, position.qz);
        bodyInterface.SetRotation(bodyId, JPH::Quat(q.x, q.y, q.z, q.w),
                                  JPH::EActivation::Activate);
      }
    }
  }
}

bool allOverworldAvatarsFacingMazePreview(const ServerGame& game,
                                          float maxYawErrorRad) {
  if (game.active_players.size() != 4) return false;

  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      return false;
    }
    const auto& position =
        game.registry.get<shared::Position>(slots.overworld_avatar);
    if (yawErrorRad(position, shared::maze_preview::kLookAtX,
                    shared::maze_preview::kLookAtY) > maxYawErrorRad) {
      return false;
    }
  }
  return true;
}

}  // namespace maze_camera
