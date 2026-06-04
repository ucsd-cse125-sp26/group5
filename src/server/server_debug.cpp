#include "server/server_debug.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

#include "server/game/credits_trigger.h"
#include "server/game/fall_challenge.h"
#include "server/game/maze.h"
#include "server/game/overworld.h"
#include "server/game/puzzles/maze/puzzle.h"
#include "server/game/puzzles/maze/trigger.h"
#include "server/game/puzzles/tangram/puzzle.h"
#include "server/game/puzzles/tangram/trigger.h"
#include "server/game/section_puzzle.h"
#include "server/game/summer_escape.h"
#include "server/server_game.h"
#include "server/server_memory_system.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/dev_spawn.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/sound_constants.h"

namespace server_debug {
namespace {

using Season = shared::SectionSeasonMap;

// Full teleport (Position + Velocity + Jolt body) for every connected overworld
// avatar; `targetForSlot` returns the destination for a 1..4 join slot. Modeled
// on summer_escape::debugSnapAllPlayersToSummerPad. Position updates ride the
// next per-tick UPDATE_ENTITY broadcast.
void teleportAllPlayers(
    ServerGame& game, const std::function<glm::vec3(uint8_t)>& targetForSlot) {
  auto& bi = game.physics.getBodyInterface();
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    uint8_t slot = 1;
    if (game.registry.all_of<shared::RenderInfo>(avatar)) {
      const uint8_t s =
          game.registry.get<shared::RenderInfo>(avatar).playerSlot;
      if (s >= 1 && s <= 4) slot = s;
    }
    const glm::vec3 t = targetForSlot(slot);
    auto& pos = game.registry.get<shared::Position>(avatar);
    pos.x = t.x;
    pos.y = t.y;
    pos.z = t.z;
    if (game.registry.all_of<shared::Velocity>(avatar)) {
      auto& vel = game.registry.get<shared::Velocity>(avatar);
      vel.dx = vel.dy = vel.dz = 0.0f;
    }
    if (game.registry.all_of<shared::PhysicsBody>(avatar)) {
      JPH::BodyID body(game.registry.get<shared::PhysicsBody>(avatar).bodyId);
      if (bi.IsAdded(body)) {
        bi.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                       JPH::EActivation::Activate);
        bi.SetLinearVelocity(body, JPH::Vec3::sZero());
        bi.SetAngularVelocity(body, JPH::Vec3::sZero());
      }
    }
  }
}

Season currentActiveSeason(ServerGame& game) {
  auto gs = game.registry.view<shared::GameSection>();
  for (auto e : gs) return gs.get<shared::GameSection>(e).currentActiveSeason;
  return Season::WINTER;
}

// Reveal one season's fragment (idempotent — no-op if already shown). Mirrors
// the old KEY_DEBUG_SPAWN_FRAGMENT path.
void revealFragmentForSeason(ServerGame& game, Season season) {
  auto frags = game.registry.view<shared::FragmentComponent>();
  for (auto fe : frags) {
    if (frags.get<shared::FragmentComponent>(fe).season != season) continue;
    if (game.registry.all_of<shared::RenderInfo>(fe))
      return;  // already revealed
    game.registry.emplace<shared::RenderInfo>(fe, "fragment", 0.25f, 0.25f,
                                              0.25f);
    if (game.network != nullptr) {
      auto buf =
          serializeEntities(game.registry, game.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, {fe}, false);
      net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
    }
    return;
  }
}

void setSeason(ServerGame& game, Season season) {
  // One-way: disables MoveInMainMap's winter clamp so the chosen season sticks.
  game.debugSeasonOverride = true;
  section_puzzle::setActiveSeason(game, season);
  colorizeSection(game, season);
  printf("[DebugPanel] set season to %s\n",
         section_puzzle::sceneNameForSeason(season));
}

void cycleSeason(ServerGame& game) {
  Season next;
  switch (currentActiveSeason(game)) {
    case Season::WINTER:
      next = Season::SPRING;
      break;
    case Season::SPRING:
      next = Season::SUMMER;
      break;
    case Season::SUMMER:
      next = Season::FALL;
      break;
    case Season::FALL:
      next = Season::WINTER;
      break;
    default:
      next = Season::WINTER;
      break;
  }
  setSeason(game, next);
}

glm::vec3 overworldSpawnFor(ServerGame& game, uint8_t slot) {
  if (shared::dev_spawn::kOverworldSpawn ==
      shared::dev_spawn::OverworldSpawn::Tangram) {
    return tangram_trigger::overworldSpawnPosition(game, slot);
  }
  return maze_trigger::overworldSpawnPosition(game, slot);
}

void teleportToFallZone(ServerGame& game) {
  const auto& L = game.fallLayout;
  static constexpr float kQuad[4][2] = {
      {-3.0f, -3.0f}, {3.0f, -3.0f}, {-3.0f, 3.0f}, {3.0f, 3.0f}};
  teleportAllPlayers(game, [&](uint8_t slot) {
    const int i = (slot >= 1 && slot <= 4) ? slot - 1 : 0;
    return glm::vec3(L.playCenterX + kQuad[i][0], L.playCenterY + kQuad[i][1],
                     L.playCenterZ);
  });
}

void startPuzzle(ServerGame& game, Season season) {
  switch (season) {
    case Season::WINTER:
      maze_puzzle::beginPuzzle(game);
      break;
    case Season::SPRING:
      tangram_puzzle::beginPuzzle(game);
      break;
    // Fall/summer auto-activate from their update() once players are in-zone.
    case Season::FALL:
      teleportToFallZone(game);
      break;
    case Season::SUMMER:
      summer_escape::debugSnapAllPlayersToSummerPad(game);
      break;
  }
}

// Stop a live minigame so the world returns to a clean overworld (no orphaned
// tangram pieces / falling cubes / shrink zone).
void tearDownMinigame(ServerGame& game, Season season) {
  switch (season) {
    case Season::WINTER:
      if (maze_puzzle::isPuzzleActive(game)) maze_puzzle::endPuzzle(game);
      break;
    case Season::SPRING:
      if (tangram_puzzle::isPuzzleActive(game)) tangram_puzzle::endPuzzle(game);
      break;
    case Season::FALL:
      for (auto e : game.registry.view<shared::FallChallengeState>()) {
        game.registry.get<shared::FallChallengeState>(e).active = false;
        break;
      }
      break;
    case Season::SUMMER:
      for (auto e : game.registry.view<shared::SummerEscapeState>()) {
        game.registry.get<shared::SummerEscapeState>(e).active = false;
        break;
      }
      break;
  }
}

// "Finish" — pretend the puzzle minigame was just won: end the minigame and
// make its fragment appear (with the solved fanfare), WITHOUT collecting it.
// The fragment then sits in the world ready to be picked up (organically or via
// the Pickup Fragment button).
void finishPuzzle(ServerGame& game, Season season) {
  tearDownMinigame(game, season);

  // The real tangram solve drops the spring fragment onto the arena trigger;
  // mirror that placement so it lands where the live puzzle would leave it.
  if (season == Season::SPRING) {
    const auto& A = game.tangramArena;
    for (auto fe : game.registry.view<shared::FragmentComponent>()) {
      if (game.registry.get<shared::FragmentComponent>(fe).season !=
          Season::SPRING) {
        continue;
      }
      if (game.registry.all_of<shared::Position>(fe)) {
        auto& p = game.registry.get<shared::Position>(fe);
        p.x = A.triggerCenterX;
        p.y = A.triggerCenterY;
        p.z = A.platformTopZ() + 2.0f;
      }
      break;
    }
  }

  revealFragmentForSeason(game, season);

  if (game.network != nullptr) {
    shared::SoundEventPacket s;
    s.soundId = static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED);
    s.volume = 1.0f;
    s.positional = false;
    net::broadcastPacket(game.network->getHost(), s);
  }
  printf("[DebugPanel] finished (revealed fragment for) season %d\n",
         static_cast<int>(season));
}

// "Pickup Fragment" — run the EXACT organic pickup chain for this season's
// fragment, so the section completes, color restores, the season advances, the
// next section unlocks, and barriers drop — identical to a real fragment
// pickup.
void pickupFragment(ServerGame& game, Season season) {
  tearDownMinigame(game, season);
  for (auto fe : game.registry.view<shared::FragmentComponent>()) {
    if (game.registry.get<shared::FragmentComponent>(fe).season != season) {
      continue;
    }
    if (game.registry.get<shared::FragmentComponent>(fe).isPickedUp) return;
    CompleteFragmentPickup(game, fe);
    return;
  }
}

// Teleport players just OUTSIDE a puzzle's trigger region (backed off along -Y
// and spread laterally) so they're next to the puzzle and can walk in to start
// it — never onto the trigger/pad, which would auto-activate.
void teleportToPuzzle(ServerGame& game, Season season) {
  static constexpr float kLat[4] = {-3.0f, -1.0f, 1.0f, 3.0f};
  static constexpr float kMargin = 4.0f;
  float cx, cy, cz, backoff;
  switch (season) {
    case Season::WINTER: {
      const auto& L = game.mazeLayout;
      cx = L.triggerCenterX;
      cy = L.triggerCenterY;
      cz = L.triggerCenterZ;
      backoff = L.halfExtent + kMargin;
      break;
    }
    case Season::SPRING: {
      const auto& A = game.tangramArena;
      cx = A.triggerCenterX;
      cy = A.triggerCenterY;
      cz = A.spawnHeightZ;
      backoff = A.halfExtent + kMargin;
      break;
    }
    case Season::SUMMER: {
      const auto& S = game.summerLayout;
      cx = S.padCenterX;
      cy = S.padCenterY;
      cz = S.padCenterZ;
      backoff = S.padHalfExtent + kMargin;
      break;
    }
    case Season::FALL: {
      const auto& F = game.fallLayout;
      cx = F.triggerCenterX;
      cy = F.triggerCenterY;
      cz = F.triggerCenterZ;
      backoff = F.triggerHalfY + kMargin;
      break;
    }
    default:
      return;
  }
  teleportAllPlayers(game, [=](uint8_t slot) {
    const int i = (slot >= 1 && slot <= 4) ? slot - 1 : 0;
    return glm::vec3(cx + kLat[i], cy - backoff, cz);
  });
}

// Toggle Jolt collision on the overworld section barriers (old B-key action).
void toggleBarrierCollision(ServerGame& game) {
  auto view = game.registry.view<shared::SectionBarrierTag,
                                 shared::OverworldTag, shared::PhysicsBody>();
  auto& bi = game.physics.getBodyInterface();
  for (auto barrier : view) {
    JPH::BodyID id(view.get<shared::PhysicsBody>(barrier).bodyId);
    if (bi.IsAdded(id)) {
      bi.RemoveBody(id);
    } else {
      bi.AddBody(id, JPH::EActivation::DontActivate);
    }
  }
  printf("[DebugPanel] toggled barrier collision\n");
}

// Toggle barrier RenderInfo + despawn/respawn so clients pick up the change
// (old N-key action).
void toggleBarrierVisibility(ServerGame& game) {
  auto view =
      game.registry.view<shared::SectionBarrierTag, shared::OverworldTag>();
  for (auto barrier : view) {
    auto& tag = view.get<shared::SectionBarrierTag>(barrier);
    const uint32_t eid = game.registry.get<shared::Entity>(barrier).id;
    if (game.registry.all_of<shared::SectionBarrierVisible>(barrier)) {
      game.registry.remove<shared::RenderInfo>(barrier);
      game.registry.remove<shared::SectionBarrierVisible>(barrier);
    } else {
      game.registry.emplace<shared::RenderInfo>(
          barrier, "cube", tag.halfExtents.x * 2.0f, tag.halfExtents.y * 2.0f,
          tag.halfExtents.z * 2.0f);
      game.registry.emplace<shared::SectionBarrierVisible>(barrier);
    }
    if (game.network == nullptr) continue;
    shared::DespawnPacket despawn;
    despawn.type = shared::PacketType::DESPAWN_ENTITY;
    despawn.entityId = eid;
    net::broadcastPacket(game.network->getHost(), despawn);
    std::vector<entt::entity> toRespawn = {barrier};
    auto buf =
        serializeEntities(game.registry, game.componentRegistry,
                          shared::PacketType::SPAWN_ENTITY, toRespawn, false);
    net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
  }
  printf("[DebugPanel] toggled barrier visibility\n");
}

void resetPlayersToOverworldSpawn(ServerGame& game) {
  teleportAllPlayers(
      game, [&](uint8_t slot) { return overworldSpawnFor(game, slot); });
  printf("[DebugPanel] reset players to overworld spawn\n");
}

void triggerCredits(ServerGame& game) {
  if (game.creditsRolled) {
    printf("[DebugPanel] credits already rolled this run — ignoring\n");
    return;
  }
  game.creditsRolled = true;
  shared::StateChangePacket pkt;
  pkt.state = shared::GameStateType::CREDITS;
  if (game.network != nullptr) {
    net::broadcastPacket(game.network->getHost(), pkt);
  }
  printf("[DebugPanel] credits triggered\n");
}

void printPositions(ServerGame& game) {
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity a = slots.overworld_avatar;
    if (!game.registry.valid(a) || !game.registry.all_of<shared::Position>(a)) {
      continue;
    }
    const auto& pos = game.registry.get<shared::Position>(a);
    const uint8_t slot =
        game.registry.all_of<shared::RenderInfo>(a)
            ? game.registry.get<shared::RenderInfo>(a).playerSlot
            : 0;
    printf("[DebugPanel] slot=%u pos=(%.2f, %.2f, %.2f)\n",
           static_cast<unsigned>(slot), pos.x, pos.y, pos.z);
  }
}

}  // namespace

void processPendingCommands(ServerGame& game) {
  using enum shared::DebugCommand;
  if (game.pendingDebugCommands.empty()) return;

  std::vector<shared::DebugCommandPacket> cmds;
  cmds.swap(game.pendingDebugCommands);
  for (const auto& c : cmds) {
    const auto season = static_cast<Season>(c.arg & 0x3u);
    switch (c.cmd) {
      case SET_SEASON:
        setSeason(game, season);
        break;
      case CYCLE_SEASON:
        cycleSeason(game);
        break;
      case SPAWN_FRAGMENT_CURRENT:
        revealFragmentForSeason(game, currentActiveSeason(game));
        break;
      case SPAWN_FRAGMENT_ALL:
        for (Season s :
             {Season::WINTER, Season::FALL, Season::SUMMER, Season::SPRING}) {
          revealFragmentForSeason(game, s);
        }
        break;
      case START_PUZZLE:
        startPuzzle(game, season);
        break;
      case FINISH_PUZZLE:
        finishPuzzle(game, season);
        break;
      case PICKUP_FRAGMENT:
        pickupFragment(game, season);
        break;
      case TELEPORT_TO_PUZZLE:
        teleportToPuzzle(game, season);
        break;
      case TOGGLE_BARRIER_COLLISION:
        toggleBarrierCollision(game);
        break;
      case TOGGLE_BARRIER_VISIBILITY:
        toggleBarrierVisibility(game);
        break;
      case RESET_TO_OVERWORLD_SPAWN:
        resetPlayersToOverworldSpawn(game);
        break;
      case TRIGGER_CREDITS:
        triggerCredits(game);
        break;
      case PRINT_POSITIONS:
        printPositions(game);
        break;
    }
  }
}

}  // namespace server_debug
