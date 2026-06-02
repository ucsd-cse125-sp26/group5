#include "server/game/fall_challenge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <random>
#include <vector>

#include "server/server_game.h"     // ServerGame, new_entity, serializeEntities
#include "server/server_network.h"  // net::broadcastRaw / broadcastPacket
#include "shared/components.h"
#include "shared/net/packet_utils.h"
#include "shared/sound_constants.h"

namespace fall_challenge {
namespace {

void falling_objects_system(ServerGame& game, float dt) {
  static std::mt19937 rng(1337);

  // Defer spawns: emplacing Position/OverworldTag on new entities while the
  // zone view (over those same pools) is being iterated can invalidate it.
  std::vector<glm::vec3> spawnPositions;

  auto zones = game.registry.view<shared::FallingHazardZone, shared::Position,
                                  shared::OverworldTag>();
  for (auto zoneEnt : zones) {
    auto& zone = zones.get<shared::FallingHazardZone>(zoneEnt);
    auto& center = zones.get<shared::Position>(zoneEnt);
    zone.timer += dt;
    if (zone.timer >= zone.interval) {
      zone.timer = 0.0f;
      // Uniform sampling over a disk: sqrt(u) keeps it from clumping at center.
      std::uniform_real_distribution<float> angDist(0.0f, glm::two_pi<float>());
      std::uniform_real_distribution<float> rDist(0.0f, 1.0f);
      float ang = angDist(rng);
      float r = zone.radius * std::sqrt(rDist(rng));
      spawnPositions.emplace_back(center.x + std::cos(ang) * r,
                                  center.y + std::sin(ang) * r,
                                  center.z + zone.spawnHeight);
    }
  }

  for (const auto& pos : spawnPositions) {
    auto [id, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, pos.x, pos.y, pos.z, 1.0f,
                                            0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "cube", 0.5f, 0.5f, 0.5f);
    game.registry.emplace<shared::OverworldTag>(
        ent);  // REQUIRED to sync/render
    game.registry.emplace<shared::FallingObject>(ent);
    JPH::BodyID body =
        game.physics.createFallingObjectBody(glm::vec3(0.25f), pos);

    std::uniform_real_distribution<float> spin(-6.0f, 6.0f);  // rad/s, tune
    game.physics.getBodyInterface().SetAngularVelocity(
        body, JPH::Vec3(spin(rng), spin(rng), spin(rng)));
    game.registry.emplace<shared::PhysicsBody>(
        ent, body.GetIndexAndSequenceNumber());

    auto buf =
        serializeEntities(game.registry, game.componentRegistry,
                          shared::PacketType::SPAWN_ENTITY, {ent}, false);
    net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
  }

  // Cleanup by lifetime OR falling out of the world. Lifetime is what stops
  // landed cubes from piling up on the floor forever.
  std::vector<entt::entity> dead;
  auto falling =
      game.registry
          .view<shared::FallingObject, shared::Position, shared::Entity>();
  for (auto ent : falling) {
    auto& fo = falling.get<shared::FallingObject>(ent);
    fo.age += dt;
    bool belowWorld = falling.get<shared::Position>(ent).z < -5.0f;
    if (fo.age > 4.0f || belowWorld) dead.push_back(ent);
  }
  for (auto ent : dead) {
    shared::DespawnPacket pkt;
    pkt.type = shared::PacketType::DESPAWN_ENTITY;
    pkt.entityId = game.registry.get<shared::Entity>(ent).id;
    net::broadcastPacket(game.network->getHost(), pkt);
    game.registry.destroy(ent);  // fires on_destroy<PhysicsBody> → destroyBody
  }
}

void knockback_system(ServerGame& game, float dt) {
  auto& bi = game.physics.getBodyInterface();

  auto players =
      game.registry
          .view<shared::Position, shared::PhysicsBody, shared::PlayerInput>();
  auto cubes = game.registry.view<shared::FallingObject, shared::Position>();

  for (auto p : players) {
    auto& ppos = players.get<shared::Position>(p);
    if (!game.registry.all_of<shared::Knockback>(p))
      game.registry.emplace<shared::Knockback>(p);
    auto& kb = game.registry.get<shared::Knockback>(p);

    JPH::BodyID pid(game.registry.get<shared::PhysicsBody>(p).bodyId);

    // The player box is tall and Position sits near the feet, so a cube resting
    // on the head is metres above ppos.z. The old test used a single 3D radius,
    // which folded that big dz into the distance and pushed overhead cubes
    // outside the 2 m sphere — so they just sat on your head with no hit. Fix:
    // decouple the axes. Keep a tight HORIZONTAL radius, but test Z against the
    // FULL height of the player's collision box (plus slack) so a cube anywhere
    // in the vertical column — including directly overhead — registers.
    float bodyMinZ = 0.0f, bodyMaxZ = 0.0f;
    bool haveSpan = game.physics.getBodyWorldZSpan(pid, bodyMinZ, bodyMaxZ);
    const float kCubeHalf = 0.25f;  // matches createFallingObjectBody extent
    const float kZSlack = 0.25f;
    float zLo = haveSpan ? bodyMinZ - kCubeHalf - kZSlack : ppos.z - 1.0f;
    float zHi = haveSpan ? bodyMaxZ + kCubeHalf + kZSlack : ppos.z + 4.0f;
    float bodyMidZ = haveSpan ? (bodyMinZ + bodyMaxZ) * 0.5f : ppos.z;

    // Nearest overlapping cube this frame, "nearest" measured in the horizontal
    // plane since that's what drives the push direction.
    bool contact = false;
    bool overhead = false;
    float bestD2 = 1e9f, bestDx = 0.0f, bestDy = 0.0f;
    const float hitRadius = 2.0f;  // horizontal reach; tune up if needed
    for (auto c : cubes) {
      auto& cpos = cubes.get<shared::Position>(c);
      float dx = ppos.x - cpos.x;
      float dy = ppos.y - cpos.y;
      float horiz2 = dx * dx + dy * dy;
      bool inColumn = horiz2 < hitRadius * hitRadius;
      bool inHeight = cpos.z >= zLo && cpos.z <= zHi;
      if (inColumn && inHeight) {
        contact = true;
        if (horiz2 < bestD2) {
          bestD2 = horiz2;
          bestDx = dx;
          bestDy = dy;
          // Above the player's vertical midpoint = landing on top of them.
          overhead = cpos.z > bodyMidZ;
        }
      }
    }

    if (contact) {
      bool newHit = !kb.contact;  // rising edge: first frame of this contact
      float horiz2 = bestDx * bestDx + bestDy * bestDy;

      // Pick the push direction ONCE, on the first frame, and latch it in
      // lastDir for the duration of contact. Re-rolling every frame (the cube
      // can rest on you for many frames) would jitter you in place instead of
      // letting you slide out from under it.
      if (newHit) {
        if (horiz2 > 0.04f) {
          // Clear horizontal offset: shove straight away from the cube.
          float hlen = std::sqrt(horiz2);
          kb.lastDirX = bestDx / hlen;
          kb.lastDirY = bestDy / hlen;
        } else {
          // Cube basically straight overhead — no offset to push along. Throw
          // the player out in a random cardinal direction so they slide clear
          // and the block drops off, rather than getting launched upward.
          static std::mt19937 kbRng(2025);
          static constexpr float kCardinals[4][2] = {
              {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
          int d = std::uniform_int_distribution<int>(0, 3)(kbRng);
          kb.lastDirX = kCardinals[d][0];
          kb.lastDirY = kCardinals[d][1];
        }
      }

      const float push = 12.0f;
      // Overhead hits push purely horizontally — adding upward velocity every
      // frame the block sits on you is what made you fly away. Side hits still
      // get a single upward pop on the first frame (never repeated, so it can't
      // accumulate).
      float upPush = (newHit && !overhead) ? 4.0f : 0.0f;
      JPH::Vec3 v = bi.GetLinearVelocity(pid);
      bi.SetLinearVelocity(
          pid,
          JPH::Vec3(kb.lastDirX * push, kb.lastDirY * push, v.GetZ() + upPush));
      kb.remaining = 0.4f;
    }

    kb.justHit = contact && !kb.contact;  // rising edge = one discrete hit
    kb.contact = contact;
    if (kb.remaining > 0.0f) kb.remaining -= dt;
  }
}

void maybeActivateFallChallenge(ServerGame& game) {
  entt::entity ctrl = entt::null;
  auto cview = game.registry.view<shared::FallChallengeState>();
  for (auto e : cview) {
    ctrl = e;
    break;
  }
  if (ctrl == entt::null) return;
  auto& cs = game.registry.get<shared::FallChallengeState>(ctrl);
  if (cs.active || cs.completed) return;  // don't restart a finished challenge

  // Activation fires when every connected player is inside the trigger box.
  const float cx = game.fallLayout.triggerCenterX;
  const float cy = game.fallLayout.triggerCenterY;
  const float cz = game.fallLayout.triggerCenterZ;
  const float hx = game.fallLayout.triggerHalfX;
  const float hy = game.fallLayout.triggerHalfY;
  constexpr float hz = 60.0f;

  int connected = 0, insideCount = 0;
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    entt::entity p = slots.overworld_avatar;
    if (!game.registry.valid(p) || !game.registry.all_of<shared::Position>(p))
      continue;
    ++connected;
    auto& pos = game.registry.get<shared::Position>(p);
    if (std::abs(pos.x - cx) <= hx && std::abs(pos.y - cy) <= hy &&
        std::abs(pos.z - cz) <= hz)
      ++insideCount;
  }
  // Dynamic: fires when every CONNECTED player is inside, whatever the count.
  if (connected > 0 && insideCount == connected) cs.active = true;
}

void fall_challenge_system(ServerGame& game, float dt) {
  static float clock = 0.0f;
  clock += dt;

  entt::entity ctrl = entt::null;
  auto cview = game.registry.view<shared::FallChallengeState>();
  for (auto e : cview) {
    ctrl = e;
    break;
  }
  if (ctrl == entt::null) return;
  auto& cs = game.registry.get<shared::FallChallengeState>(ctrl);
  if (!cs.active) return;

  // Play-zone bounds: players survive inside this area while cubes rain above.
  const float cx = game.fallLayout.playCenterX;
  const float cy = game.fallLayout.playCenterY;
  const float cz = game.fallLayout.playCenterZ;
  const float hx = game.fallLayout.playHalfX;
  const float hy = game.fallLayout.playHalfY;
  constexpr float hz = 60.0f;

  bool present[4] = {false, false, false, false};
  bool inZone[4] = {false, false, false, false};
  uint8_t mask = 0;  // replicated: who is inside the zone right now
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    entt::entity p = slots.overworld_avatar;
    if (!game.registry.valid(p) || !game.registry.all_of<shared::RenderInfo>(p))
      continue;
    int slot = game.registry.get<shared::RenderInfo>(p).playerSlot;
    if (slot < 1 || slot > 4) continue;
    int i = slot - 1;
    present[i] = true;

    if (game.registry.all_of<shared::Position>(p)) {
      auto& pos = game.registry.get<shared::Position>(p);
      if (std::abs(pos.x - cx) <= hx && std::abs(pos.y - cy) <= hy &&
          std::abs(pos.z - cz) <= hz) {
        inZone[i] = true;
        mask |= (1u << i);
      }
    }

    if (!game.registry.all_of<shared::FallHitWindow>(p))
      game.registry.emplace<shared::FallHitWindow>(p);
    auto& win = game.registry.get<shared::FallHitWindow>(p);

    if (!inZone[i]) {
      // Player stepped out — freeze their fill, don't penalise or reward.
      // The HUD hides their bar segment because participantMask won't have
      // their bit set; their fill value is preserved for when they return.
      continue;
    }

    bool justHit = game.registry.all_of<shared::Knockback>(p) &&
                   game.registry.get<shared::Knockback>(p).justHit;
    if (justHit) {
      win.times[win.head] = clock;
      win.head = (win.head + 1) % 4;
      cs.fill[i] = std::max(0.0f, cs.fill[i] - cs.hitPenalty);

      int recent = 0;
      for (float th : win.times)
        if (clock - th <= 4.0f) ++recent;
      if (recent >= 4) cs.fill[i] = 0.0f;
    } else {
      cs.fill[i] = std::min(1.0f, cs.fill[i] + cs.fillRate * dt);
    }
  }

  cs.participantMask = mask;
  // Solve only counts players currently inside the zone.
  bool anyoneInZone = (mask != 0);
  bool solved = anyoneInZone;
  for (int i = 0; i < 4; ++i)
    if (inZone[i] && cs.fill[i] < 1.0f) solved = false;
  if (solved) {
    cs.active = false;
    cs.completed = true;

    // Stop spawning new cubes.
    std::vector<entt::entity> zoneEnts;
    auto zones = game.registry.view<shared::FallingHazardZone>();
    for (auto e : zones) zoneEnts.push_back(e);
    for (auto e : zoneEnts) game.registry.remove<shared::FallingHazardZone>(e);

    // Clear every cube still in the air or on the floor right now, so nothing
    // is left lying around after the challenge ends.
    std::vector<entt::entity> deadCubes;
    auto cubes = game.registry.view<shared::FallingObject, shared::Entity>();
    for (auto e : cubes) deadCubes.push_back(e);
    for (auto e : deadCubes) {
      shared::DespawnPacket dpkt;
      dpkt.type = shared::PacketType::DESPAWN_ENTITY;
      dpkt.entityId = game.registry.get<shared::Entity>(e).id;
      net::broadcastPacket(game.network->getHost(), dpkt);
      game.registry.destroy(e);
    }

    shared::SoundEventPacket pkt;
    pkt.soundId = static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED);
    pkt.volume = 1.0f;
    pkt.positional = false;
    net::broadcastPacket(game.network->getHost(), pkt);

    // Reveal the fall fragment — created without RenderInfo in the level
    // loader, so it's been invisible until now. Adding RenderInfo +
    // re-broadcasting SPAWN_ENTITY makes it pop in for all clients.
    auto frags = game.registry.view<shared::FragmentComponent>();
    for (auto fe : frags) {
      if (frags.get<shared::FragmentComponent>(fe).season !=
          shared::SectionSeasonMap::FALL)
        continue;
      if (game.registry.all_of<shared::RenderInfo>(fe))
        continue;  // already shown
      game.registry.emplace<shared::RenderInfo>(fe, "light_cube", 0.5f);
      auto buf =
          serializeEntities(game.registry, game.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, {fe}, false);
      net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
    }
  }
}

}  // namespace

bool isActive(ServerGame& game) {
  auto v = game.registry.view<shared::FallChallengeState>();
  for (auto e : v) return v.get<shared::FallChallengeState>(e).active;
  return false;
}

void CollectFallFragment(ServerGame& game) {
  // Find the fall section controller.
  entt::entity fallSect = entt::null;
  auto sectView = game.registry.view<shared::SectionController>();
  for (auto e : sectView) {
    if (sectView.get<shared::SectionController>(e).type ==
        shared::SectionSeasonMap::FALL) {
      fallSect = e;
      break;
    }
  }
  if (fallSect == entt::null) return;

  auto& section = game.registry.get<shared::SectionController>(fallSect);

  // Find and finish the linked puzzle entity.
  auto entityView = game.registry.view<shared::Entity>();
  for (auto e : entityView) {
    if (entityView.get<shared::Entity>(e).id != section.puzzleID) continue;
    if (!game.registry.all_of<shared::PuzzleComponent>(e)) break;
    auto& puzzle = game.registry.get<shared::PuzzleComponent>(e);
    if (puzzle.phase == shared::RunPhase::FINISHED) break;
    puzzle.phase = shared::RunPhase::FINISHED;
    break;
  }

  if (section.completed) return;
  section.completed = true;

  for (auto e : game.registry.view<shared::GameSection>()) {
    auto& gs = game.registry.get<shared::GameSection>(e);
    if (gs.sectionsCompleted < 255) gs.sectionsCompleted++;
  }
  printf("[GameLogic] CollectFallFragment: fall done, sections completed++\n");
}

void update(ServerGame& game, float dt) {
  maybeActivateFallChallenge(game);
  if (!isActive(game)) return;
  falling_objects_system(game, dt);
  knockback_system(game, dt);
  fall_challenge_system(game, dt);
}

}  // namespace fall_challenge
