#include "server/game/summer_escape.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <glm/glm.hpp>
#include <random>

#include "server/game/section_puzzle.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/input.h"
#include "shared/net/packet_utils.h"
#include "shared/sound_constants.h"

namespace summer_escape {
namespace {

std::mt19937& rng() {
  static std::mt19937 engine(0x53756d6du);  // "Summ"
  return engine;
}

entt::entity controllerEntity(ServerGame& game) {
  if (game.registry.valid(game.summerEscapeController) &&
      game.registry.all_of<shared::SummerEscapeState>(
          game.summerEscapeController)) {
    return game.summerEscapeController;
  }
  auto view = game.registry.view<shared::SummerEscapeState>();
  for (auto e : view) return e;
  return entt::null;
}

void playSound(ServerGame& game, shared::SoundId id) {
  if (game.network == nullptr) return;
  shared::SoundEventPacket pkt;
  pkt.soundId = static_cast<uint32_t>(id);
  pkt.volume = 1.0f;
  pkt.positional = false;
  net::broadcastPacket(game.network->getHost(), pkt);
}

// Choose a target region whose sides are `shrinkFactor` of the current region,
// placed at a uniformly random offset fully inside the current region.
void pickTarget(ServerGame& game, shared::SummerEscapeState& s) {
  const float f = game.summerLayout.shrinkFactor;
  const float w = (s.regMaxX - s.regMinX) * f;
  const float h = (s.regMaxY - s.regMinY) * f;
  const float freeX = std::max(0.0f, (s.regMaxX - s.regMinX) - w);
  const float freeY = std::max(0.0f, (s.regMaxY - s.regMinY) - h);
  std::uniform_real_distribution<float> ux(0.0f, freeX);
  std::uniform_real_distribution<float> uy(0.0f, freeY);
  s.tgtMinX = s.regMinX + ux(rng());
  s.tgtMinY = s.regMinY + uy(rng());
  s.tgtMaxX = s.tgtMinX + w;
  s.tgtMaxY = s.tgtMinY + h;
}

void scatterPlayersToStartPositions(ServerGame& game) {
  auto& bodyInterface = game.physics.getBodyInterface();
  const auto& L = game.summerLayout;
  const float cx = (L.regionMinX + L.regionMaxX) * 0.5f;
  const float cy = (L.regionMinY + L.regionMaxY) * 0.5f;
  const float ox = (L.regionMaxX - L.regionMinX) * 0.22f;
  const float oy = (L.regionMaxY - L.regionMinY) * 0.22f;
  const float spawnX[4] = {cx - ox, cx + ox, cx - ox, cx + ox};
  const float spawnY[4] = {cy - oy, cy - oy, cy + oy, cy + oy};

  int order = 0;
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    int idx = order % 4;
    if (game.registry.all_of<shared::RenderInfo>(avatar)) {
      const uint8_t slot =
          game.registry.get<shared::RenderInfo>(avatar).playerSlot;
      if (slot >= 1 && slot <= 4) idx = slot - 1;
    }
    auto& pos = game.registry.get<shared::Position>(avatar);
    pos.x = shared::summer::Layout::clampX(spawnX[idx]);
    pos.y = shared::summer::Layout::clampY(spawnY[idx]);
    pos.z = L.playFloorZ;
    if (game.registry.all_of<shared::Velocity>(avatar)) {
      auto& vel = game.registry.get<shared::Velocity>(avatar);
      vel.dx = vel.dy = vel.dz = 0.0f;
    }
    if (game.registry.all_of<shared::PhysicsBody>(avatar)) {
      JPH::BodyID body(game.registry.get<shared::PhysicsBody>(avatar).bodyId);
      if (bodyInterface.IsAdded(body)) {
        bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                                  JPH::EActivation::Activate);
        bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
      }
    }
    ++order;
  }
}

// Begin (or fully restart) a run: reset to wave 0, the initial region, and
// scatter players back to their start corners.
void beginRun(ServerGame& game, shared::SummerEscapeState& s) {
  s.active = true;
  s.completed = false;
  s.wave = 0;
  s.regMinX = game.summerLayout.regionMinX;
  s.regMinY = game.summerLayout.regionMinY;
  s.regMaxX = game.summerLayout.regionMaxX;
  s.regMaxY = game.summerLayout.regionMaxY;
  game.summerWaveStartRegion =
      glm::vec4(s.regMinX, s.regMinY, s.regMaxX, s.regMaxY);
  game.summerWaveElapsed = 0.0f;
  game.summerGrace = game.summerLayout.startGraceSec;
  pickTarget(game, s);
  scatterPlayersToStartPositions(game);
}

void maybeActivate(ServerGame& game, shared::SummerEscapeState& s) {
  if (s.active || s.completed) return;
  if (!section_puzzle::isSectionUnlocked(game,
                                         shared::SectionSeasonMap::SUMMER)) {
    return;
  }
  int connected = 0;
  int inside = 0;
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    ++connected;
    const auto& pos = game.registry.get<shared::Position>(avatar);
    if (game.summerLayout.isInsidePad(pos.x, pos.y)) ++inside;
  }
  if (connected > 0 && inside == connected) beginRun(game, s);
}

bool anyPlayerOutsideRegion(ServerGame& game,
                            const shared::SummerEscapeState& s) {
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    const auto& pos = game.registry.get<shared::Position>(avatar);
    if (!shared::summer::Layout::isInsideRegion(
            pos.x, pos.y, s.regMinX, s.regMinY, s.regMaxX, s.regMaxY)) {
      return true;
    }
  }
  return false;
}

void setSummerPuzzleFinished(ServerGame& game) {
  const entt::entity sect =
      section_puzzle::findSection(game, shared::SectionSeasonMap::SUMMER);
  if (sect == entt::null) return;
  const uint32_t puzzleId =
      game.registry.get<shared::SectionController>(sect).puzzleID;
  auto view = game.registry.view<shared::Entity, shared::PuzzleComponent>();
  for (auto e : view) {
    if (view.get<shared::Entity>(e).id != puzzleId) continue;
    view.get<shared::PuzzleComponent>(e).phase = shared::RunPhase::FINISHED;
    break;
  }
}

// Reveal the summer fragment (created invisible in the level loader) by adding
// RenderInfo and broadcasting a fresh SPAWN_ENTITY, mirroring the fall/spring
// reveal flow.
void revealSummerFragment(ServerGame& game) {
  auto frags = game.registry.view<shared::FragmentComponent>();
  for (auto fe : frags) {
    if (frags.get<shared::FragmentComponent>(fe).season !=
        shared::SectionSeasonMap::SUMMER) {
      continue;
    }
    if (game.registry.all_of<shared::RenderInfo>(fe)) continue;
    game.registry.emplace<shared::RenderInfo>(fe, "light_cube", 0.5f);
    auto buf = serializeEntities(game.registry, game.componentRegistry,
                                 shared::PacketType::SPAWN_ENTITY, {fe}, false);
    net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
  }
}

void solve(ServerGame& game, shared::SummerEscapeState& s) {
  setSummerPuzzleFinished(game);
  revealSummerFragment(game);
  playSound(game, shared::SoundId::PUZZLE_SOLVED);
  s.active = false;
  s.completed = true;
  printf("[Summer] All waves survived — summer fragment revealed\n");
}

}  // namespace

void debugSnapAllPlayersToSummerPad(ServerGame& game) {
  auto& bodyInterface = game.physics.getBodyInterface();
  const auto& L = game.summerLayout;
  static constexpr float kPadOffsets[4][2] = {
      {-2.5f, -2.5f}, {2.5f, -2.5f}, {-2.5f, 2.5f}, {2.5f, 2.5f}};

  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    const entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    int idx = 0;
    if (game.registry.all_of<shared::RenderInfo>(avatar)) {
      const uint8_t slot =
          game.registry.get<shared::RenderInfo>(avatar).playerSlot;
      if (slot >= 1 && slot <= 4) idx = slot - 1;
    }
    auto& pos = game.registry.get<shared::Position>(avatar);
    pos.x = L.padCenterX + kPadOffsets[idx][0];
    pos.y = L.padCenterY + kPadOffsets[idx][1];
    pos.z = L.playFloorZ;
    if (game.registry.all_of<shared::Velocity>(avatar)) {
      auto& vel = game.registry.get<shared::Velocity>(avatar);
      vel.dx = vel.dy = vel.dz = 0.0f;
    }
    if (game.registry.all_of<shared::PhysicsBody>(avatar)) {
      JPH::BodyID body(game.registry.get<shared::PhysicsBody>(avatar).bodyId);
      if (bodyInterface.IsAdded(body)) {
        bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                                  JPH::EActivation::Activate);
        bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
        bodyInterface.SetAngularVelocity(body, JPH::Vec3::sZero());
      }
    }
  }
  printf("[Summer] Debug: snapped %zu players to summer pad (z=%.1f)\n",
         game.active_players.size(), L.playFloorZ);
}

bool isActive(ServerGame& game) {
  const entt::entity c = controllerEntity(game);
  if (c == entt::null) return false;
  return game.registry.get<shared::SummerEscapeState>(c).active;
}

void update(ServerGame& game, float dt) {
  const entt::entity c = controllerEntity(game);
  if (c == entt::null) return;
  auto& s = game.registry.get<shared::SummerEscapeState>(c);

  maybeActivate(game, s);
  if (!s.active) return;

  // Q cancels the run (players keep their current positions).
  for (auto e :
       game.registry.view<shared::PlayerInput, shared::OverworldTag>()) {
    const auto& in = game.registry.get<shared::PlayerInput>(e);
    if (in.keys_newly_pressed & KEY_EXIT_MINIGAME) {
      s.active = false;
      return;
    }
  }

  if (game.summerGrace > 0.0f) {
    game.summerGrace -= dt;
    // Hold the full wave-start region and do not advance the shrink timer while
    // players settle after teleport (shrinking during grace put corner spawns
    // outside the box and caused a restart loop).
    const glm::vec4& hold = game.summerWaveStartRegion;
    s.regMinX = hold.x;
    s.regMinY = hold.y;
    s.regMaxX = hold.z;
    s.regMaxY = hold.w;
    return;
  }

  if (anyPlayerOutsideRegion(game, s)) {
    playSound(game, shared::SoundId::PUZZLE_FAILED);
    beginRun(game, s);  // full reset
    return;
  }

  const int wi = s.wave < shared::summer::Layout::kWaveCount
                     ? s.wave
                     : shared::summer::Layout::kWaveCount - 1;
  const float dur = game.summerLayout.waveDurationSec[wi] > 0.0001f
                        ? game.summerLayout.waveDurationSec[wi]
                        : 0.0001f;

  game.summerWaveElapsed += dt;
  float t = game.summerWaveElapsed / dur;
  if (t > 1.0f) t = 1.0f;

  const glm::vec4& a = game.summerWaveStartRegion;
  s.regMinX = a.x + (s.tgtMinX - a.x) * t;
  s.regMinY = a.y + (s.tgtMinY - a.y) * t;
  s.regMaxX = a.z + (s.tgtMaxX - a.z) * t;
  s.regMaxY = a.w + (s.tgtMaxY - a.w) * t;

  if (game.summerWaveElapsed >= dur) {
    s.regMinX = s.tgtMinX;
    s.regMinY = s.tgtMinY;
    s.regMaxX = s.tgtMaxX;
    s.regMaxY = s.tgtMaxY;
    s.wave = static_cast<uint8_t>(s.wave + 1);
    if (s.wave >= shared::summer::Layout::kWaveCount) {
      solve(game, s);
      return;
    }
    game.summerWaveStartRegion =
        glm::vec4(s.regMinX, s.regMinY, s.regMaxX, s.regMaxY);
    game.summerWaveElapsed = 0.0f;
    pickTarget(game, s);
  }
}

void CollectSummerFragment(ServerGame& game) {
  if (section_puzzle::isSectionCompleted(game,
                                         shared::SectionSeasonMap::SUMMER)) {
    return;
  }
  setSummerPuzzleFinished(game);
  section_puzzle::completeSection(game, shared::SectionSeasonMap::SUMMER);
  printf("[Summer] CollectSummerFragment: summer done, sections completed++\n");
}

}  // namespace summer_escape
