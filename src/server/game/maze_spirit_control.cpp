#include "server/game/maze_spirit_control.h"

#include <cmath>

#include "server/server_game.h"
#include "shared/components.h"
#include "shared/input.h"

namespace maze_spirit_control {

entt::entity findSharedSpirit(const ServerGame& game) {
  for (auto e : game.registry.view<shared::MazeSpiritGrid, shared::MazeTag>()) {
    return e;
  }
  return entt::null;
}

namespace {

void applyPadKeyToDrive(SpiritDrive& out, shared::MazeDirection pad,
                        InputKeys keys, bool& pushed) {
  switch (pad) {
    case shared::MazeDirection::UP:
      if ((keys & KEY_SPIRIT_UP) != 0) {
        out.dy += 1.0f;
        pushed = true;
      }
      break;
    case shared::MazeDirection::DOWN:
      if ((keys & KEY_SPIRIT_DOWN) != 0) {
        out.dy -= 1.0f;
        pushed = true;
      }
      break;
    case shared::MazeDirection::LEFT:
      if ((keys & KEY_SPIRIT_LEFT) != 0) {
        out.dx -= 1.0f;
        pushed = true;
      }
      break;
    case shared::MazeDirection::RIGHT:
      if ((keys & KEY_SPIRIT_RIGHT) != 0) {
        out.dx += 1.0f;
        pushed = true;
      }
      break;
    default:
      break;
  }
}

SpiritDrive collectSpiritDriveFromTag(const ServerGame& game,
                                      bool skipSpiritGrid) {
  SpiritDrive out;
  auto collect = [&](entt::entity ent) {
    if (skipSpiritGrid && game.registry.all_of<shared::MazeSpiritGrid>(ent)) {
      return;
    }
    if (game.registry.all_of<shared::OverworldMazePiece>(ent)) return;

    const auto& in = game.registry.get<shared::PlayerInput>(ent);
    bool pushed = false;

    if (!skipSpiritGrid &&
        game.registry.all_of<shared::MazePadBinding>(ent)) {
      const auto pad = game.registry.get<shared::MazePadBinding>(ent).pad;
      applyPadKeyToDrive(out, pad, in.keys, pushed);
    } else {
      if ((in.keys & KEY_SPIRIT_UP) != 0) {
        out.dy += 1.0f;
        pushed = true;
      }
      if ((in.keys & KEY_SPIRIT_DOWN) != 0) {
        out.dy -= 1.0f;
        pushed = true;
      }
      if ((in.keys & KEY_SPIRIT_LEFT) != 0) {
        out.dx -= 1.0f;
        pushed = true;
      }
      if ((in.keys & KEY_SPIRIT_RIGHT) != 0) {
        out.dx += 1.0f;
        pushed = true;
      }
    }
    if (pushed) out.activePushCount++;
  };

  if (skipSpiritGrid) {
    auto inputView = game.registry.view<shared::PlayerInput, shared::MazeTag>();
    for (auto ent : inputView) collect(ent);
  } else {
    auto inputView =
        game.registry.view<shared::PlayerInput, shared::OverworldTag>();
    for (auto ent : inputView) collect(ent);
  }
  return out;
}

}  // namespace

SpiritDrive collectSpiritDriveFromPlayers(const ServerGame& game) {
  return collectSpiritDriveFromTag(game, true);
}

SpiritDrive collectSpiritDriveFromOverworldPlayers(const ServerGame& game) {
  return collectSpiritDriveFromTag(game, false);
}

void applySpiritDriveVelocity(ServerGame& game, entt::entity spirit,
                              const SpiritDrive& drive) {
  if (!game.registry.all_of<shared::PhysicsBody>(spirit)) return;

  auto& bodyInterface = game.physics.getBodyInterface();
  auto& spiritPb = game.registry.get<shared::PhysicsBody>(spirit);
  JPH::BodyID spiritBody(spiritPb.bodyId);
  if (!bodyInterface.IsAdded(spiritBody)) return;

  constexpr float kSpeed = 10.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  const float len = std::sqrt(drive.dx * drive.dx + drive.dy * drive.dy);
  if (len > 1e-5f) {
    vx = (drive.dx / len) * kSpeed;
    vy = (drive.dy / len) * kSpeed;
  }

  JPH::Vec3 curVel = bodyInterface.GetLinearVelocity(spiritBody);
  bodyInterface.SetLinearVelocity(spiritBody, JPH::Vec3(vx, vy, curVel.GetZ()));

  if (game.registry.all_of<shared::Velocity>(spirit)) {
    auto& vel = game.registry.get<shared::Velocity>(spirit);
    vel.dx = vx;
    vel.dy = vy;
    vel.dz = curVel.GetZ();
  }
}

}  // namespace maze_spirit_control
