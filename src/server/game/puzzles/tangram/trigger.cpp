#include "server/game/puzzles/tangram/trigger.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cstdio>

#include "server/game/section_puzzle.h"
#include "server/server_game.h"
#include "shared/log.h"
#include "shared/puzzles/tangram/defaults.h"
#include "shared/puzzles/tangram/puzzle_data.h"

namespace tangram_trigger {

bool isInsideTypingTriggerRegion(const shared::Position& position,
                                 const shared::tangram::ArenaLayout& layout) {
  return layout.isInsideTrigger(position.x, position.y);
}

bool isInsideTypingTriggerRegion(const shared::Position& position,
                                 const ServerGame& game) {
  return isInsideTypingTriggerRegion(position, game.tangramArena);
}

bool allActivePlayersInTangramTrigger(const ServerGame& game) {
  if (game.active_players.size() <
      static_cast<size_t>(shared::tangram_puzzle::kRequiredPlayersForStart)) {
    return false;
  }
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      return false;
    }
    const auto& position =
        game.registry.get<shared::Position>(slots.overworld_avatar);
    if (!isInsideTypingTriggerRegion(position, game)) return false;
  }
  return true;
}

bool isFallTypingAvailable(const ServerGame& game) {
  return section_puzzle::isSectionAvailable(game,
                                            shared::SectionSeasonMap::SPRING);
}

bool canTriggerTangram(const ServerGame& game) {
  return !section_puzzle::isSectionCompleted(game,
                                             shared::SectionSeasonMap::SPRING);
}

glm::vec3 overworldSpawnPosition(const shared::tangram::ArenaLayout& layout,
                                 uint8_t joinSlot) {
  const int idx =
      (joinSlot >= 1 && joinSlot <= 4) ? static_cast<int>(joinSlot) - 1 : 0;
  return {layout.spawnBaseX + layout.spawnOffsetX[idx],
          layout.spawnBaseY + layout.spawnOffsetY[idx], layout.spawnHeightZ};
}

glm::vec3 overworldSpawnPosition(const ServerGame& game, uint8_t joinSlot) {
  return overworldSpawnPosition(game.tangramArena, joinSlot);
}

void snapAllPlayersToTangramPad(ServerGame& game) {
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      continue;
    }
    uint8_t slot = 1;
    if (game.registry.all_of<shared::RenderInfo>(slots.overworld_avatar)) {
      slot = game.registry.get<shared::RenderInfo>(slots.overworld_avatar)
                 .playerSlot;
      if (slot < 1 || slot > 4) slot = 1;
    }
    const glm::vec3 spawn = overworldSpawnPosition(game, slot);
    auto& pos = game.registry.get<shared::Position>(slots.overworld_avatar);
    pos.x = spawn.x;
    pos.y = spawn.y;
    pos.z = spawn.z;
    if (game.registry.all_of<shared::Velocity>(slots.overworld_avatar)) {
      auto& vel = game.registry.get<shared::Velocity>(slots.overworld_avatar);
      vel.dx = vel.dy = vel.dz = 0.0f;
    }
    if (game.registry.all_of<shared::PhysicsBody>(slots.overworld_avatar)) {
      auto& pb = game.registry.get<shared::PhysicsBody>(slots.overworld_avatar);
      auto& bodyInterface = game.physics.getBodyInterface();
      JPH::BodyID bodyId(pb.bodyId);
      if (bodyInterface.IsAdded(bodyId)) {
        JPH::RVec3 p(pos.x, pos.y, pos.z);
        bodyInterface.SetPosition(bodyId, p, JPH::EActivation::Activate);
        bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3::sZero());
      }
    }
  }
  LOG_DEBUG("[Tangram] Snapped %zu players to tangram pad\n",
            game.active_players.size());
}

void keepPlayersOnTangramPlatform(ServerGame& game) {
  const shared::tangram::ArenaLayout& layout = game.tangramArena;
  const float minZ = layout.spawnHeightZ;
  const float hx = layout.platformScaleX * 0.5f + 1.5f;
  const float hy = layout.platformScaleY * 0.5f + 1.5f;
  auto& bodyInterface = game.physics.getBodyInterface();

  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position, shared::PhysicsBody>(avatar)) {
      continue;
    }
    auto& pos = game.registry.get<shared::Position>(avatar);
    if (std::abs(pos.x - layout.platformCenterX) > hx ||
        std::abs(pos.y - layout.platformCenterY) > hy) {
      continue;
    }
    if (pos.z >= minZ) continue;

    pos.z = minZ;
    if (game.registry.all_of<shared::Velocity>(avatar)) {
      auto& vel = game.registry.get<shared::Velocity>(avatar);
      vel.dz = 0.0f;
    }
    auto& pb = game.registry.get<shared::PhysicsBody>(avatar);
    JPH::BodyID body(pb.bodyId);
    if (!bodyInterface.IsAdded(body)) continue;
    bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                              JPH::EActivation::Activate);
    JPH::Vec3 v = bodyInterface.GetLinearVelocity(body);
    bodyInterface.SetLinearVelocity(body, JPH::Vec3(v.GetX(), v.GetY(), 0.0f));
  }
}

}  // namespace tangram_trigger
