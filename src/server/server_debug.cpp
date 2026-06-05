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
#include "shared/log.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/puzzles/summer/layout.h"
#include "shared/puzzles/tangram/roles.h"
#include "shared/sound_constants.h"

namespace server_debug {
namespace {

using Season = shared::SectionSeasonMap;

// Full teleport (Position + Velocity + Jolt body) for every connected overworld
// avatar; `targetForSlot` returns the destination for a 1..4 join slot. Modeled
// on summer_escape::debugSnapAllPlayersToSummerPad. Position updates ride the
// next per-tick UPDATE_ENTITY broadcast.
// `onlySlot` in 1..4 restricts the teleport to that join slot; 0 = all players.
void teleportAllPlayers(ServerGame& game,
                        const std::function<glm::vec3(uint8_t)>& targetForSlot,
                        uint8_t onlySlot = 0) {
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
    if (onlySlot != 0 && slot != onlySlot) continue;
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
  syncOverworldSeasonMusic(game);
  LOG_DEBUG("[DebugPanel] set season to %s\n",
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
  LOG_DEBUG("[DebugPanel] finished (revealed fragment for) season %d\n",
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

// Destination just OUTSIDE a puzzle's trigger region (backed off along -Y and
// spread laterally by join slot) so a player lands next to the puzzle and can
// walk in to start it — never onto the trigger/pad, which would auto-activate.
glm::vec3 puzzleSlotTarget(ServerGame& game, Season season, uint8_t slot) {
  static constexpr float kLat[4] = {-3.0f, -1.0f, 1.0f, 3.0f};
  static constexpr float kMargin = 4.0f;
  float cx = 0.0f, cy = 0.0f, cz = 0.0f, backoff = 0.0f;
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
  }
  const int i = (slot >= 1 && slot <= 4) ? slot - 1 : 0;
  return {cx + kLat[i], cy - backoff, cz};
}

void teleportToPuzzle(ServerGame& game, Season season) {
  teleportAllPlayers(
      game, [&](uint8_t slot) { return puzzleSlotTarget(game, season, slot); });
}

// Teleport ONE player (by join slot) to a puzzle area or the overworld spawn.
void teleportPlayer(ServerGame& game, uint8_t slot,
                    shared::DebugTeleportDest dest) {
  if (slot < 1 || slot > 4) return;
  using D = shared::DebugTeleportDest;
  std::function<glm::vec3(uint8_t)> target;
  switch (dest) {
    case D::WINTER_PUZZLE:
      target = [&](uint8_t s) {
        return puzzleSlotTarget(game, Season::WINTER, s);
      };
      break;
    case D::FALL_PUZZLE:
      target = [&](uint8_t s) {
        return puzzleSlotTarget(game, Season::FALL, s);
      };
      break;
    case D::SUMMER_PUZZLE:
      target = [&](uint8_t s) {
        return puzzleSlotTarget(game, Season::SUMMER, s);
      };
      break;
    case D::SPRING_PUZZLE:
      target = [&](uint8_t s) {
        return puzzleSlotTarget(game, Season::SPRING, s);
      };
      break;
    case D::OVERWORLD_SPAWN:
      target = [&](uint8_t s) { return overworldSpawnFor(game, s); };
      break;
    default:
      return;
  }
  teleportAllPlayers(game, target, slot);
  LOG_DEBUG("[DebugPanel] teleported slot %u to dest %u\n",
            static_cast<unsigned>(slot), static_cast<unsigned>(dest));
}

// ── Winter maze: rebind a player's single directional power. ──────────────
// dir == NONE removes the binding entirely, which lets that player drive with
// ALL four arrow keys (maze_spirit_control reads every arrow when unbound).
void setMazePower(ServerGame& game, uint8_t slot, shared::MazeDirection dir) {
  if (slot < 1 || slot > 4) return;
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::RenderInfo>(avatar)) {
      continue;
    }
    if (game.registry.get<shared::RenderInfo>(avatar).playerSlot != slot) {
      continue;
    }
    if (dir == shared::MazeDirection::NONE) {
      game.registry.remove<shared::MazePadBinding>(avatar);
      LOG_DEBUG("[DebugPanel] maze: slot %u granted ALL directions\n",
                static_cast<unsigned>(slot));
    } else {
      game.registry.emplace_or_replace<shared::MazePadBinding>(avatar, dir);
      LOG_DEBUG("[DebugPanel] maze: slot %u bound to direction %u\n",
                static_cast<unsigned>(slot), static_cast<unsigned>(dir));
    }
    return;
  }
}

// ── Spring tangram: set role-isolation stage live (0 = everyone everything).
// ──
void setTangramStage(ServerGame& game, uint8_t stage) {
  if (stage > shared::tangram_roles::kStagePushP1) {
    stage = shared::tangram_roles::kStagePushP1;
  }
  tangram_puzzle::setIsolationStage(game, stage);
  LOG_DEBUG("[DebugPanel] tangram isolation stage -> %u\n",
            static_cast<unsigned>(stage));
}

// ── Spring tangram: per-player ability grant (layered over the stage). ─────
void setTangramGrant(ServerGame& game, uint8_t slot,
                     shared::DebugTangramAbility ability, bool enable) {
  tangram_puzzle::setPlayerGrant(game, slot, static_cast<uint8_t>(ability),
                                 enable);
  LOG_DEBUG("[DebugPanel] tangram grant: slot %u ability %u -> %s\n",
            static_cast<unsigned>(slot), static_cast<unsigned>(ability),
            enable ? "on" : "off");
}

// ── Fall challenge difficulty (applies live to the running controller/zone).
// ──
void setFallParam(ServerGame& game, shared::DebugFallParam param, float value) {
  using P = shared::DebugFallParam;
  if (param == P::FILL_RATE || param == P::HIT_PENALTY) {
    for (auto e : game.registry.view<shared::FallChallengeState>()) {
      auto& cs = game.registry.get<shared::FallChallengeState>(e);
      if (param == P::FILL_RATE)
        cs.fillRate = value;
      else
        cs.hitPenalty = value;
    }
  } else {
    for (auto e : game.registry.view<shared::FallingHazardZone>()) {
      auto& z = game.registry.get<shared::FallingHazardZone>(e);
      if (param == P::SPAWN_INTERVAL)
        z.interval = value;
      else if (param == P::BURSTS_TO_SWITCH)
        z.burstsUntilSwitch = static_cast<int>(value);
    }
  }
  LOG_DEBUG("[DebugPanel] fall param %u -> %.3f\n",
            static_cast<unsigned>(param), value);
}

// ── Summer escape difficulty (read live from summerLayout each tick). ──────
void setSummerParam(ServerGame& game, shared::DebugSummerParam param,
                    float value) {
  using P = shared::DebugSummerParam;
  switch (param) {
    case P::SHRINK_FACTOR:
      game.summerLayout.shrinkFactor = value;
      break;
    case P::WAVE_DURATION:
      for (float& i : game.summerLayout.waveDurationSec) {
        i = value;
      }
      break;
    case P::START_GRACE:
      game.summerLayout.startGraceSec = value;
      break;
    default:
      return;
  }
  LOG_DEBUG("[DebugPanel] summer param %u -> %.3f\n",
            static_cast<unsigned>(param), value);
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
  LOG_DEBUG("[DebugPanel] toggled barrier collision\n");
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
  LOG_DEBUG("[DebugPanel] toggled barrier visibility\n");
}

void resetPlayersToOverworldSpawn(ServerGame& game) {
  teleportAllPlayers(
      game, [&](uint8_t slot) { return overworldSpawnFor(game, slot); });
  LOG_DEBUG("[DebugPanel] reset players to overworld spawn\n");
}

// Force the credits roll, even if it already played this run. Resetting the
// latch also re-arms the natural Fallen-house auto-trigger. Clients restart the
// scroll from the top because the StateChangePacket bumps the credits epoch
// (the client resets creditsStartTime whenever it (re)enters the CREDITS
// state).
void triggerCredits(ServerGame& game) {
  game.creditsRolled = true;  // suppress the natural auto-trigger double-fire
  shared::StateChangePacket pkt;
  pkt.state = shared::GameStateType::CREDITS;
  if (game.network != nullptr) {
    net::broadcastPacket(game.network->getHost(), pkt);
  }
  LOG_DEBUG("[DebugPanel] credits (re)rolled\n");
}

void toggleDebugLog() {
  shared::log::setDebugEnabled(!shared::log::debugEnabled);
  LOG_DEBUG("[DebugPanel] server debug log %s\n",
            shared::log::debugEnabled ? "enabled" : "disabled");
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
    LOG_DEBUG("[DebugPanel] slot=%u pos=(%.2f, %.2f, %.2f)\n",
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
      case TOGGLE_DEBUG_LOG:
        toggleDebugLog();
        break;
      case SET_MAZE_POWER:
        setMazePower(game, static_cast<uint8_t>(c.arg),
                     static_cast<shared::MazeDirection>(c.arg2));
        break;
      case SET_TANGRAM_STAGE:
        setTangramStage(game, static_cast<uint8_t>(c.arg));
        break;
      case TELEPORT_PLAYER:
        teleportPlayer(game, static_cast<uint8_t>(c.arg),
                       static_cast<shared::DebugTeleportDest>(c.arg2));
        break;
      case SET_FALL_PARAM:
        setFallParam(game, static_cast<shared::DebugFallParam>(c.arg), c.farg);
        break;
      case SET_SUMMER_PARAM:
        setSummerParam(game, static_cast<shared::DebugSummerParam>(c.arg),
                       c.farg);
        break;
      case SET_TANGRAM_GRANT:
        setTangramGrant(game, static_cast<uint8_t>(c.arg),
                        static_cast<shared::DebugTangramAbility>(c.arg2),
                        c.farg > 0.5f);
        break;
    }
  }
}

}  // namespace server_debug
