#include "server/game/puzzles/tangram/camera.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>

#include "server/server_game.h"
#include "shared/components.h"

namespace tangram_camera {
namespace {

void setYawTowardTarget(shared::Position& position, float targetX,
                        float targetY) {
  const float dx = targetX - position.x;
  const float dy = targetY - position.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) return;
  const float yaw = std::atan2(-dx / len, dy / len);
  const float half = yaw * 0.5f;
  position.qw = std::cos(half);
  position.qx = 0.0f;
  position.qy = 0.0f;
  position.qz = std::sin(half);
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
  if (flatLen < 1e-4f) return std::numbers::pi_v<float>;
  flat /= flatLen;
  const float actualYaw = std::atan2(-flat.x, flat.y);
  float err = desiredYaw - actualYaw;
  while (err > std::numbers::pi_v<float>) err -= 6.28318531f;
  while (err < -std::numbers::pi_v<float>) err += 6.28318531f;
  return std::abs(err);
}

}  // namespace

void snapOverworldAvatarFaceTangramBoard(ServerGame& game,
                                         entt::entity avatar) {
  if (!game.registry.valid(avatar) ||
      !game.registry.all_of<shared::Position>(avatar)) {
    return;
  }
  auto& position = game.registry.get<shared::Position>(avatar);
  setYawTowardTarget(position, game.tangramArena.lookAtX(),
                     game.tangramArena.lookAtY());

  if (game.registry.all_of<shared::PhysicsBody>(avatar)) {
    auto& pb = game.registry.get<shared::PhysicsBody>(avatar);
    auto& bodyInterface = game.physics.getBodyInterface();
    JPH::BodyID bodyId(pb.bodyId);
    if (bodyInterface.IsAdded(bodyId)) {
      glm::quat q(position.qw, position.qx, position.qy, position.qz);
      bodyInterface.SetRotation(bodyId, JPH::Quat(q.x, q.y, q.z, q.w),
                                JPH::EActivation::Activate);
      JPH::RVec3 p(position.x, position.y, position.z);
      bodyInterface.SetPosition(bodyId, p, JPH::EActivation::Activate);
    }
  }
}

void snapOverworldAvatarsFaceTangramBoard(ServerGame& game) {
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    snapOverworldAvatarFaceTangramBoard(game, slots.overworld_avatar);
  }
}

bool allOverworldAvatarsFacingTangramBoard(const ServerGame& game,
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
    if (yawErrorRad(position, game.tangramArena.lookAtX(),
                    game.tangramArena.lookAtY()) > maxYawErrorRad) {
      return false;
    }
  }
  return true;
}

}  // namespace tangram_camera
