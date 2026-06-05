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
#include "shared/log.h"
#include "shared/net/packet_utils.h"
#include "shared/sound_constants.h"

namespace fall_challenge {
namespace {

// ── Pattern burst helper ─────────────────────────────────────────────────────
// Returns a list of world-space XY spawn positions (Z is filled in by the
// caller using zone.spawnHeight). Mutates zone.patternAngle / patternStep so
// successive bursts rotate / advance automatically.
//
// player_positions: overworld avatar XY coords — used only by Aimed pattern.
// Pass an empty vector when no player data is available; Aimed falls back to
// Spiral for the non-aimed slots.
static std::vector<glm::vec3> burstPositions(
    shared::FallingHazardZone& zone,  // non-const: mutates angle/step
    const shared::Position& center, std::mt19937& rng,
    const std::vector<glm::vec2>& playerPositions) {
  using Pattern = shared::FallingHazardZone::AttackPattern;
  std::vector<glm::vec3> out;

  const float cx = center.x;
  const float cy = center.y;
  const float R = zone.radius;

  // ── Random (original behaviour) ──────────────────────────────────────────
  if (zone.pattern == Pattern::Random) {
    std::uniform_real_distribution<float> angD(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> rD(0.0f, 1.0f);
    float ang = angD(rng);
    float r = R * std::sqrt(rD(rng));
    out.emplace_back(cx + std::cos(ang) * r, cy + std::sin(ang) * r, 0.0f);
    return out;
  }

  // ── Spiral (Fibonacci / sunflower) ───────────────────────────────────────
  // Fires `count` objects whose positions are the next `count` points on the
  // sunflower lattice, advancing patternStep each burst. After a full
  // revolution the lattice wraps naturally, so coverage stays even forever.
  if (zone.pattern == Pattern::Spiral) {
    constexpr int count = 7;
    constexpr float goldenAngle = 2.39996323f;  // radians, ~137.5°
    for (int i = 0; i < count; ++i) {
      int idx = zone.patternStep * count + i;
      float r = R * std::sqrt(static_cast<float>(idx + 1) /
                              static_cast<float>(count * 12 + 1));
      // clamp r so we never exceed the disk radius on the first few steps
      r = std::min(r, R * 0.97f);
      float ang = static_cast<float>(idx) * goldenAngle + zone.patternAngle;
      out.emplace_back(cx + std::cos(ang) * r, cy + std::sin(ang) * r, 0.0f);
    }
    // Advance: after 12 bursts the pattern has visited the whole disk once.
    // Reset step so it loops rather than drifting to huge idx values.
    zone.patternStep = (zone.patternStep + 1) % 12;
    zone.burstsSinceSwitch++;
    if (zone.burstsSinceSwitch >= zone.burstsUntilSwitch) {
      zone.burstsSinceSwitch = 0;
      std::uniform_int_distribution<int> switchD(5, 12);
      zone.burstsUntilSwitch = switchD(rng);

      using P = shared::FallingHazardZone::AttackPattern;
      std::uniform_int_distribution<int> patD(1, 3);
      switch (patD(rng)) {
        case 1:
          zone.pattern = P::Spiral;
          break;
        case 2:
          zone.pattern = P::Spokes;
          break;
        case 3:
          zone.pattern = P::Aimed;
          break;
      }
      zone.patternAngle = 0.0f;
      zone.patternStep = 0;
    }

    return out;  // unreachable but satisfies compiler
  }

  // ── Spokes ───────────────────────────────────────────────────────────────
  // Each burst fires one object per spoke at evenly-spaced radii, then
  // rotates the whole pattern by (2π / spokeCount / subSteps) so over
  // `subSteps` bursts every gap between spokes has been hit.
  if (zone.pattern == Pattern::Spokes) {
    constexpr int spokeCount = 5;
    constexpr int perSpoke = 3;  // objects per spoke (inner→outer)
    constexpr int subSteps = 4;  // how many rotations before full repeat

    float baseAngle = zone.patternAngle;

    for (int s = 0; s < spokeCount; ++s) {
      float spokeAng = baseAngle + (glm::two_pi<float>() / spokeCount) * s;
      for (int d = 0; d < perSpoke; ++d) {
        // Evenly space radii from 20% to 95% of disk radius.
        float t = 0.2f + 0.75f * (static_cast<float>(d) /
                                  static_cast<float>(perSpoke - 1));
        float r = R * t;
        out.emplace_back(cx + std::cos(spokeAng) * r,
                         cy + std::sin(spokeAng) * r, 0.0f);
      }
    }
    if (zone.patternStep % 3 == 0) {
      constexpr float kCornerT = 0.82f;
      constexpr float kCornerAngles[4] = {
          glm::quarter_pi<float>(),
          glm::quarter_pi<float>() * 3.0f,
          glm::quarter_pi<float>() * 5.0f,
          glm::quarter_pi<float>() * 7.0f,
      };
      std::uniform_real_distribution<float> jD(-0.6f, 0.6f);
      for (float ca : kCornerAngles) {
        float ang = ca + zone.patternAngle + jD(rng);
        out.emplace_back(cx + std::cos(ang) * R * kCornerT,
                         cy + std::sin(ang) * R * kCornerT, 0.0f);
      }
    }
    // Rotate by one sub-step increment each burst.
    zone.patternAngle +=
        (glm::two_pi<float>() / spokeCount) / static_cast<float>(subSteps);
    zone.patternStep = (zone.patternStep + 1) % (spokeCount * subSteps);
    return out;
  }

  // ── Aimed ────────────────────────────────────────────────────────────────
  // Fires one object directly at each player (clamped inside the disk so it
  // still lands on the platform). Then fills remaining slots with spiral
  // points so total coverage stays even even when player count is low.
  if (zone.pattern == Pattern::Aimed) {
    constexpr int totalCount = 7;
    constexpr float aimJitter = 0.8f;  // metres of random offset on aimed shot

    std::uniform_real_distribution<float> jitterD(-aimJitter, aimJitter);

    // Aimed shots at each player.
    int aimed = 0;
    for (const auto& pp : playerPositions) {
      if (aimed >= totalCount) break;
      float tx = pp.x + jitterD(rng);
      float ty = pp.y + jitterD(rng);
      // Clamp inside disk.
      float dx = tx - cx, dy = ty - cy;
      float d = std::sqrt(dx * dx + dy * dy);
      if (d > R * 0.95f) {
        tx = cx + dx / d * R * 0.95f;
        ty = cy + dy / d * R * 0.95f;
      }
      out.emplace_back(tx, ty, 0.0f);
      ++aimed;
    }

    // Fill remaining with spiral so the whole disk gets coverage.
    constexpr float goldenAngle = 2.39996323f;
    int remaining = totalCount - aimed;
    for (int i = 0; i < remaining; ++i) {
      int idx = zone.patternStep * remaining + i;
      float r = R * std::sqrt(static_cast<float>(idx + 1) /
                              static_cast<float>(remaining * 12 + 1));
      r = std::min(r, R * 0.97f);
      float ang = static_cast<float>(idx) * goldenAngle + zone.patternAngle;
      out.emplace_back(cx + std::cos(ang) * r, cy + std::sin(ang) * r, 0.0f);
    }

    zone.patternAngle += 0.31f;  // slow drift so aimed + fill rotate over time
    zone.patternStep = (zone.patternStep + 1) % 12;
    return out;
  }

  return out;  // unreachable but satisfies compiler
}

// Falling-object size knobs. kHalf is the physics half-extent (sphere radius)
// AND the knockback hit-half — they must stay equal, so they share this. Render
// scale is separate (it scales the mesh, not a world radius).
constexpr float kFallingObjectHalf = 1.1f;         // physics + knockback
constexpr float kFallingObjectRenderScale = 1.2f;  // visual mesh scale
constexpr float kKnockbackPush = 16.0f;            // horizontal shove force
constexpr float kKnockbackUpPush = 8.0f;           // vertical pop on side hit
constexpr float kKnockbackDuration = 0.6f;         // seconds the shove lasts
constexpr float kKnockbackHitRadius = 2.0f;  // horizontal detection radius
constexpr float kCleanUpDroppings =
    4.0f;  // jhust how long objects live once dropped

void falling_objects_system(ServerGame& game, float dt) {
  static std::mt19937 rng(1337);

  // Defer spawns: emplacing Position/OverworldTag on new entities while the
  // zone view (over those same pools) is being iterated can invalidate it.
  std::vector<glm::vec3> spawnPositions;

  // auto zones = game.registry.view<shared::FallingHazardZone,
  // shared::Position,
  //                                 shared::OverworldTag>();
  // for (auto zoneEnt : zones) {
  //   auto& zone = zones.get<shared::FallingHazardZone>(zoneEnt);
  //   auto& center = zones.get<shared::Position>(zoneEnt);
  //   zone.timer += dt;
  //   if (zone.timer >= zone.interval) {
  //     zone.timer = 0.0f;
  //     if (zone.shape == shared::FallingHazardZone::Shape::Rect) {
  //       // Uniform over an axis-aligned rectangle — full square-platform
  //       cover. std::uniform_real_distribution<float> xDist(-zone.halfX,
  //       zone.halfX); std::uniform_real_distribution<float> yDist(-zone.halfY,
  //       zone.halfY); spawnPositions.emplace_back(center.x + xDist(rng),
  //                                   center.y + yDist(rng),
  //                                   center.z + zone.spawnHeight);
  //     }
  //     else {
  //       // Uniform over a disk: sqrt(u) keeps it from clumping at the center.
  //       std::uniform_real_distribution<float> angDist(0.0f,
  //                                                     glm::two_pi<float>());
  //       std::uniform_real_distribution<float> rDist(0.0f, 1.0f);
  //       float ang = angDist(rng);
  //       float r = zone.radius * std::sqrt(rDist(rng));
  //       spawnPositions.emplace_back(center.x + std::cos(ang) * r,
  //                                   center.y + std::sin(ang) * r,
  //                                   center.z + zone.spawnHeight);
  //     }
  //   }
  // }

  // Collect player XY positions once — used by the Aimed pattern.
  std::vector<glm::vec2> playerPositions;
  {
    auto players = game.registry.view<shared::Position, shared::PlayerInput,
                                      shared::OverworldTag>();
    for (auto p : players)
      playerPositions.emplace_back(players.get<shared::Position>(p).x,
                                   players.get<shared::Position>(p).y);
  }

  auto zones = game.registry.view<shared::FallingHazardZone, shared::Position,
                                  shared::OverworldTag>();
  for (auto zoneEnt : zones) {
    auto& zone = zones.get<shared::FallingHazardZone>(zoneEnt);
    auto& center = zones.get<shared::Position>(zoneEnt);
    zone.timer += dt;
    if (zone.timer < zone.interval) continue;
    zone.timer = 0.0f;

    if (zone.shape == shared::FallingHazardZone::Shape::Rect) {
      // Rect stays random — patterns are disk-only.
      std::uniform_real_distribution<float> xDist(-zone.halfX, zone.halfX);
      std::uniform_real_distribution<float> yDist(-zone.halfY, zone.halfY);
      spawnPositions.emplace_back(center.x + xDist(rng), center.y + yDist(rng),
                                  center.z + zone.spawnHeight);
    } else {
      // Disk — delegate to pattern helper; helper returns XY, we add Z here.
      auto burst = burstPositions(zone, center, rng, playerPositions);
      for (auto& b : burst)
        spawnPositions.emplace_back(b.x, b.y, center.z + zone.spawnHeight);
    }
  }

  for (const auto& pos : spawnPositions) {
    auto [id, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, pos.x, pos.y, pos.z, 1.0f,
                                            0.0f, 0.0f, 0.0f);
    {
      auto& ri = game.registry.emplace<shared::RenderInfo>(
          ent, "pumpkin", kFallingObjectRenderScale, kFallingObjectRenderScale,
          kFallingObjectRenderScale);
      ri.colorExempt = true;
    }
    game.registry.emplace<shared::OverworldTag>(
        ent);  // REQUIRED to sync/render
    game.registry.emplace<shared::FallingObject>(ent);
    JPH::BodyID body = game.physics.createFallingObjectBody(
        glm::vec3(kFallingObjectHalf), pos);
    if (body.IsInvalid()) {
      // Physics pool exhausted (already logged). Drop this hazard rather than
      // spawning a collisionless ghost or dereferencing a null body. No
      // PhysicsBody was emplaced, so destroy() has nothing extra to clean up.
      game.registry.destroy(ent);
      continue;
    }

    std::uniform_real_distribution<float> spin(-6.0f, 6.0f);  // rad/s, tune

    std::uniform_real_distribution<float> drift(-2.0f, 2.0f);  // m/s sideways
    game.physics.getBodyInterface().SetLinearVelocity(
        body, JPH::Vec3(drift(rng), drift(rng), 0.0f));
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
    if (fo.age > kCleanUpDroppings || belowWorld) dead.push_back(ent);
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
    const float kCubeHalf = kFallingObjectHalf;
    ;  // matches createFallingObjectBody extent
    const float kZSlack = 0.25f;
    float zLo = haveSpan ? bodyMinZ - kCubeHalf - kZSlack : ppos.z - 1.0f;
    float zHi = haveSpan ? bodyMaxZ + kCubeHalf + kZSlack : ppos.z + 4.0f;
    float bodyMidZ = haveSpan ? (bodyMinZ + bodyMaxZ) * 0.5f : ppos.z;

    // Nearest overlapping cube this frame, "nearest" measured in the horizontal
    // plane since that's what drives the push direction.
    bool contact = false;
    bool overhead = false;
    float bestD2 = 1e9f, bestDx = 0.0f, bestDy = 0.0f;
    const float hitRadius =
        kKnockbackHitRadius;  // horizontal reach; tune up if needed
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

      const float push = kKnockbackPush;
      // Overhead hits push purely horizontally — adding upward velocity every
      // frame the block sits on you is what made you fly away. Side hits still
      // get a single upward pop on the first frame (never repeated, so it can't
      // accumulate).
      float upPush = (newHit && !overhead) ? kKnockbackUpPush : 0.0f;
      JPH::Vec3 v = bi.GetLinearVelocity(pid);
      bi.SetLinearVelocity(
          pid,
          JPH::Vec3(kb.lastDirX * push, kb.lastDirY * push, v.GetZ() + upPush));
      kb.remaining = kKnockbackDuration;
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

  // Activation fires when every connected player is standing on the play
  // platform. The old trigger box was a small region offset from the visible
  // green platform (playCenter), so a single player could wander into it but
  // getting several bodies to crowd into it simultaneously was nearly
  // impossible. The platform IS the natural gather spot, so trigger on it
  // directly. A small margin lets a player on the very edge still count.
  constexpr float kEntryMargin = 1.5f;
  const float cx = game.fallLayout.playCenterX;
  const float cy = game.fallLayout.playCenterY;
  const float cz = game.fallLayout.playCenterZ;
  const float hx = game.fallLayout.playHalfX + kEntryMargin;
  const float hy = game.fallLayout.playHalfY + kEntryMargin;
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

  bool inZone[4] = {false, false, false, false};
  uint8_t connectedMask =
      0;                   // players in the game right now (valid avatar+slot)
  uint8_t inZoneMask = 0;  // players physically on the platform this frame
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    entt::entity p = slots.overworld_avatar;
    if (!game.registry.valid(p) || !game.registry.all_of<shared::RenderInfo>(p))
      continue;
    int slot = game.registry.get<shared::RenderInfo>(p).playerSlot;
    if (slot < 1 || slot > 4) continue;
    int i = slot - 1;
    connectedMask |= (1u << i);

    if (game.registry.all_of<shared::Position>(p)) {
      auto& pos = game.registry.get<shared::Position>(p);
      if (std::abs(pos.x - cx) <= hx && std::abs(pos.y - cy) <= hy &&
          std::abs(pos.z - cz) <= hz) {
        inZone[i] = true;
        inZoneMask |= (1u << i);
      }
    }

    if (!game.registry.all_of<shared::FallHitWindow>(p))
      game.registry.emplace<shared::FallHitWindow>(p);
    auto& win = game.registry.get<shared::FallHitWindow>(p);

    if (!inZone[i]) {
      // Stepped off the platform: freeze this player's fill — no reward, no
      // penalty. They stay enrolled (their bar remains and still blocks the
      // solve), and the value is preserved for when they step back on.

      // Drain fill at the same rate as a hit penalty, scaled by dt so it's
      // smooth rather than a single frame deduction.
      // cs.hitPenalty is the per-hit flat drop; divide by a hold time (seconds
      // a player would need to stand still taking hits to empty the bar) to get
      // a per-second drain rate that feels equivalent.
      constexpr float kOffPlatformDrainSeconds =
          6.0f;  // empty bar in 10s if AFK off-platform
      cs.fill[i] = std::max(
          0.0f, cs.fill[i] - (cs.hitPenalty / kOffPlatformDrainSeconds) * dt);
      continue;
    }

    bool justHit = game.registry.all_of<shared::Knockback>(p) &&
                   game.registry.get<shared::Knockback>(p).justHit;
    if (justHit) {
      win.times[win.head] = clock;
      win.head = (win.head + 1) % 4;

      // when hit by falling object progress bar decrements,
      // if hit multiple times in sliding window even more decrement
      // cs.fill[i] = std::max(0.0f, cs.fill[i] - cs.hitPenalty);

      // int recent = 0;
      // for (float th : win.times)
      //   if (clock - th <= 4.0f) ++recent;
      // if (recent >= 4) cs.fill[i] = 0.0f;
    } else {
      cs.fill[i] = std::min(1.0f, cs.fill[i] + cs.fillRate * dt);
    }
  }

  // Enrollment. A player joins the challenge the first time they set foot on
  // the platform, and stays enrolled — keeps their bar, still counts toward the
  // solve — for as long as they remain connected, even after stepping off the
  // platform. So the bar count reflects everyone who has joined and is still in
  // the game, not just whoever happens to be standing in the arena this frame.
  // Disconnecting is the only thing that removes a player (and frees their bar
  // / slot so a future occupant starts fresh).
  auto dropped =
      static_cast<uint8_t>(cs.participantMask & ~connectedMask);  // gone
  for (int i = 0; i < 4; ++i)
    if (dropped & (1u << i)) cs.fill[i] = 0.0f;
  cs.participantMask |= inZoneMask;     // new arrivals get a permanent bar
  cs.participantMask &= connectedMask;  // drop anyone who disconnected

  // Solve requires EVERY enrolled (joined + still connected) player to have
  // filled their bar. One player can't finish for the group while a teammate
  // who has joined is still working — or has stepped off mid-fill. If that
  // teammate disconnects they drop out of participantMask above, which unblocks
  // the remaining players.
  bool solved = (cs.participantMask != 0);
  for (int i = 0; i < 4; ++i) {
    bool enrolled = (cs.participantMask & (1u << i)) != 0;
    if (enrolled && cs.fill[i] < 1.0f) solved = false;
  }
  if (solved) {
    cs.active = false;
    cs.completed = true;

    // Clear in-flight knockback. Once cs.active is false, update()
    // early-returns and knockback_system never runs again, so kb.remaining can
    // no longer tick down. Any player mid-knockback on this frame would be
    // stuck (input ignored, drifting) permanently. Zero it here and kill
    // residual XY velocity.
    auto& bi = game.physics.getBodyInterface();
    auto kbView = game.registry.view<shared::Knockback>();
    for (auto e : kbView) {
      auto& kb = kbView.get<shared::Knockback>(e);
      kb.remaining = 0.0f;
      kb.contact = false;
      kb.justHit = false;
      if (game.registry.all_of<shared::PhysicsBody>(e)) {
        JPH::BodyID bid(game.registry.get<shared::PhysicsBody>(e).bodyId);
        if (bi.IsAdded(bid)) {
          JPH::Vec3 v = bi.GetLinearVelocity(bid);
          bi.SetLinearVelocity(bid, JPH::Vec3(0.0f, 0.0f, v.GetZ()));
        }
      }
    }

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
      game.registry.emplace<shared::RenderInfo>(fe, "fragment", 0.25f, 0.25f,
                                                0.25f);
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
  LOG_DEBUG(
      "[GameLogic] CollectFallFragment: fall done, sections completed++\n");
}

void update(ServerGame& game, float dt) {
  maybeActivateFallChallenge(game);
  if (!isActive(game)) return;
  falling_objects_system(game, dt);
  knockback_system(game, dt);
  fall_challenge_system(game, dt);
}

}  // namespace fall_challenge