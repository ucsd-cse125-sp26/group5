#include "server/game/puzzles/tangram/puzzle.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

#include "server/game/puzzles/maze/trigger.h"
#include "server/game/puzzles/tangram/roles.h"
#include "server/game/puzzles/tangram/trigger.h"
#include "server/game/section_puzzle.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/log.h"
#include "shared/dev_spawn.h"
#include "shared/input.h"
#include "shared/net/packet_utils.h"
#include "shared/puzzles/tangram/defaults.h"
#include "shared/puzzles/tangram/puzzle_data.h"
#include "shared/puzzles/tangram/roles.h"
#include "shared/puzzles/tangram/slot_validate.h"
#include "shared/sound_constants.h"

namespace {

struct PlatformBounds {
  float minX = 0.0f;
  float maxX = 0.0f;
  float minY = 0.0f;
  float maxY = 0.0f;
  float minZ = 0.0f;
  float maxZ = 0.0f;
};

PlatformBounds fullPlatformBounds(const ServerGame& game, float inset = 0.5f) {
  const shared::tangram::ArenaLayout& L = game.tangramArena;
  const float hx = L.platformScaleX * 0.5f - inset;
  const float hy = L.platformScaleY * 0.5f - inset;
  return PlatformBounds{
      L.platformCenterX - hx, L.platformCenterX + hx, L.platformCenterY - hy,
      L.platformCenterY + hy, L.platformTopZ(),       L.spawnHeightZ + 2.0f,
  };
}

void slotRelPose(const ServerGame& game,
                 const shared::tangram_puzzle::PieceDef& def, float& relX,
                 float& relY, float& rotRad) {
  if (shared::tangram_slot_validate::mapSlotLayoutUsable(
          game.tangramSlotLayout) &&
      def.id >= 1 && def.id <= 7) {
    const shared::tangram_slot::SlotPose& slot =
        game.tangramSlotLayout.slots[def.id - 1];
    relX = slot.relX;
    relY = slot.relY;
    rotRad = slot.rotRad;
    return;
  }
  relX = def.targetRelX;
  relY = def.targetRelY;
  rotRad = shared::tangram_puzzle::quantizeYawToRotateStep(def.targetRotRad);
}

}  // namespace

namespace tangram_puzzle {

void endPuzzle(ServerGame& game, bool releasePlayers);

namespace {

glm::vec3 slotSnapWorldPos(const ServerGame& game,
                           const shared::tangram_puzzle::PieceDef& def) {
  const shared::tangram::ArenaLayout& layout = game.tangramArena;
  float relX = 0.0f;
  float relY = 0.0f;
  float rotRad = 0.0f;
  slotRelPose(game, def, relX, relY, rotRad);
  (void)rotRad;
  return {layout.boardCenterX + relX, layout.boardCenterY + relY,
          layout.pieceRestZ(def.scaleZ)};
}

void setActiveFlag(ServerGame& game, bool active, uint8_t isolationStage = 0) {
  if (!game.registry.valid(game.overworldTangramController)) return;
  auto& state = game.registry.get<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  state.active = active;
  state.roleIsolationStage = active ? isolationStage : 0;
}

void broadcastSpawn(ServerGame& game,
                    const std::vector<entt::entity>& entities) {
  if (entities.empty() || game.network == nullptr) return;
  auto buf =
      serializeEntities(game.registry, game.componentRegistry,
                        shared::PacketType::SPAWN_ENTITY, entities, false);
  net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
}

float yawFromPosition(const shared::Position& pos) {
  // Tangram pieces only rotate around Z; prefer qw/qz to avoid Jolt xy noise.
  if (std::abs(pos.qx) < 1e-4f && std::abs(pos.qy) < 1e-4f) {
    return 2.0f * std::atan2(pos.qz, pos.qw);
  }
  return std::atan2(2.0f * (pos.qw * pos.qz + pos.qx * pos.qy),
                    1.0f - 2.0f * (pos.qy * pos.qy + pos.qz * pos.qz));
}

float flattenedYaw(const shared::Position& pos) {
  return shared::tangram_puzzle::quantizeYawToRotateStep(yawFromPosition(pos));
}

glm::quat flatQuatFromYaw(float yaw) {
  return shared::tangram_puzzle::quatFromYawRad(
      shared::tangram_puzzle::quantizeYawToRotateStep(yaw));
}

void flattenPieceRotation(ServerGame& game, entt::entity ent) {
  if (!game.registry.all_of<shared::Position>(ent)) return;
  auto& pos = game.registry.get<shared::Position>(ent);
  const glm::quat q = flatQuatFromYaw(yawFromPosition(pos));
  if (std::abs(pos.qx - q.x) < 1e-5f && std::abs(pos.qy - q.y) < 1e-5f &&
      std::abs(pos.qw - q.w) < 1e-5f && std::abs(pos.qz - q.z) < 1e-5f) {
    return;
  }
  pos.qw = q.w;
  pos.qx = q.x;
  pos.qy = q.y;
  pos.qz = q.z;

  if (!game.registry.all_of<shared::PhysicsBody>(ent)) return;
  auto& bodyInterface = game.physics.getBodyInterface();
  auto& pb = game.registry.get<shared::PhysicsBody>(ent);
  JPH::BodyID body(pb.bodyId);
  if (!bodyInterface.IsAdded(body)) return;
  bodyInterface.SetRotation(body, JPH::Quat(q.x, q.y, q.z, q.w),
                            JPH::EActivation::Activate);
  bodyInterface.SetAngularVelocity(body, JPH::Vec3::sZero());
}

float pieceFootprintMismatch(const ServerGame& game,
                             const shared::tangram_puzzle::PieceDef& def,
                             const shared::Position& pos, float relX,
                             float relY, float targetRot) {
  const shared::tangram::ArenaLayout& layout = game.tangramArena;
  const float pieceRelX = pos.x - layout.boardCenterX;
  const float pieceRelY = pos.y - layout.boardCenterY;
  const float pieceYaw = flattenedYaw(pos);

  if (shared::tangram_slot_validate::footprintsMatch(
          def, pieceRelX, pieceRelY, pieceYaw, relX, relY, targetRot,
          shared::tangram_puzzle::kWinFootprintMismatchMax)) {
    return 0.0f;
  }

  using shared::tangram_slot_validate::detail::cyclicVertexMismatch;
  using shared::tangram_slot_validate::detail::Vec2;
  using shared::tangram_slot_validate::detail::worldPolygon;
  const float slotYaw =
      shared::tangram_puzzle::quantizeYawToRotateStep(targetRot);
  const std::vector<Vec2> slotPoly = worldPolygon(def, relX, relY, slotYaw);
  const std::vector<Vec2> piecePoly =
      worldPolygon(def, pieceRelX, pieceRelY, pieceYaw);
  return cyclicVertexMismatch(piecePoly, slotPoly);
}

bool pieceAlignedWithSlot(const ServerGame& game, entt::entity ent) {
  if (!game.registry.all_of<shared::TangramPiece, shared::Position>(ent)) {
    return false;
  }
  const auto& piece = game.registry.get<shared::TangramPiece>(ent);
  const shared::tangram_puzzle::PieceDef* def =
      shared::tangram_puzzle::pieceDefForId(piece.pieceId);
  if (def == nullptr) return false;

  const auto& pos = game.registry.get<shared::Position>(ent);
  float relX = 0.0f;
  float relY = 0.0f;
  float targetRot = 0.0f;
  slotRelPose(game, *def, relX, relY, targetRot);

  const shared::tangram::ArenaLayout& layout = game.tangramArena;
  return shared::tangram_slot_validate::footprintsMatch(
      *def, pos.x - layout.boardCenterX, pos.y - layout.boardCenterY,
      flattenedYaw(pos), relX, relY, targetRot,
      shared::tangram_puzzle::kWinFootprintMismatchMax);
}

bool pieceIsNearSlot(const ServerGame& game,
                     const shared::tangram_puzzle::PieceDef& def,
                     const shared::Position& pos, float relX, float relY,
                     float targetRot) {
  const glm::vec3 slotPos = slotSnapWorldPos(game, def);
  const float centerDist = std::hypot(pos.x - slotPos.x, pos.y - slotPos.y);
  const float reach = shared::tangram_puzzle::snapRadiusForPiece(def);
  if (centerDist <= reach) {
    return true;
  }
  return shared::tangram_slot_validate::footprintsMatch(
      def, pos.x - game.tangramArena.boardCenterX,
      pos.y - game.tangramArena.boardCenterY, flattenedYaw(pos), relX, relY,
      targetRot, shared::tangram_puzzle::kSnapFootprintMismatchMax);
}

void applyPieceTransform(ServerGame& game, entt::entity ent,
                         const glm::vec3& worldPos, const glm::quat& rot) {
  auto& pos = game.registry.get<shared::Position>(ent);
  pos.x = worldPos.x;
  pos.y = worldPos.y;
  pos.z = worldPos.z;
  pos.qw = rot.w;
  pos.qx = rot.x;
  pos.qy = rot.y;
  pos.qz = rot.z;

  if (!game.registry.all_of<shared::PhysicsBody>(ent)) return;
  auto& bodyInterface = game.physics.getBodyInterface();
  auto& pb = game.registry.get<shared::PhysicsBody>(ent);
  JPH::BodyID body(pb.bodyId);
  if (!bodyInterface.IsAdded(body)) return;
  bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                            JPH::EActivation::Activate);
  bodyInterface.SetRotation(body, JPH::Quat(rot.x, rot.y, rot.z, rot.w),
                            JPH::EActivation::Activate);
  bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
  bodyInterface.SetAngularVelocity(body, JPH::Vec3::sZero());
}

void holdSnappedPieceAtSlot(ServerGame& game, entt::entity ent) {
  if (!game.registry.all_of<shared::TangramPiece, shared::Position>(ent)) {
    return;
  }
  auto& piece = game.registry.get<shared::TangramPiece>(ent);
  if (!piece.slotSnapped) return;

  const shared::tangram_puzzle::PieceDef* def =
      shared::tangram_puzzle::pieceDefForId(piece.pieceId);
  if (def == nullptr) return;

  const glm::vec3 slotPos = slotSnapWorldPos(game, *def);
  auto& pos = game.registry.get<shared::Position>(ent);
  float relX = 0.0f;
  float relY = 0.0f;
  float targetRot = 0.0f;
  slotRelPose(game, *def, relX, relY, targetRot);

  const float yaw = flattenedYaw(pos);
  const float qTarget =
      shared::tangram_puzzle::quantizeYawToRotateStep(targetRot);
  glm::quat rot = flatQuatFromYaw(yaw);
  if (shared::tangram_puzzle::yawMatchesTarget(yaw, qTarget)) {
    rot = flatQuatFromYaw(qTarget);
  }

  if (pos.x == slotPos.x && pos.y == slotPos.y && pos.z == slotPos.z &&
      std::abs(pos.qw - rot.w) < 1e-4f && std::abs(pos.qz - rot.z) < 1e-4f &&
      std::abs(pos.qx) < 1e-4f && std::abs(pos.qy) < 1e-4f) {
    return;
  }
  applyPieceTransform(game, ent, slotPos, rot);
}

std::mt19937& tangramSnapRng() {
  static std::mt19937 rng(static_cast<uint32_t>(std::random_device{}()));
  return rng;
}

void trySnapPiecesToSlots(ServerGame& game) {
  auto view = game.registry.view<shared::TangramPiece, shared::Position>();
  std::uniform_int_distribution<int> stepDist(
      0, shared::tangram_puzzle::kRotateStepCount - 1);

  for (auto ent : view) {
    auto& piece = view.get<shared::TangramPiece>(ent);
    if (piece.slotSnapped) continue;

    const shared::tangram_puzzle::PieceDef* def =
        shared::tangram_puzzle::pieceDefForId(piece.pieceId);
    if (def == nullptr) continue;

    const auto& pos = view.get<shared::Position>(ent);
    float relX = 0.0f;
    float relY = 0.0f;
    float targetRot = 0.0f;
    slotRelPose(game, *def, relX, relY, targetRot);
    if (!pieceIsNearSlot(game, *def, pos, relX, relY, targetRot)) {
      continue;
    }

    const glm::vec3 slotPos = slotSnapWorldPos(game, *def);

    piece.slotSnapped = true;
    // Lock XY to slot; random wrong facing on the 45° grid — press R to align.
    const float qTarget =
        shared::tangram_puzzle::quantizeYawToRotateStep(targetRot);
    int off = stepDist(tangramSnapRng());
    if (off == 0) off = 1;
    const float snapYaw = shared::tangram_puzzle::normalizeYawRad(
        qTarget +
        static_cast<float>(off) * shared::tangram_puzzle::kRotateStepRad);
    const glm::quat rot = shared::tangram_puzzle::quatFromYawRad(snapYaw);
    applyPieceTransform(game, ent, slotPos, rot);

    uint8_t stage = 0;
    if (game.registry.valid(game.overworldTangramController) &&
        game.registry.all_of<shared::OverworldTangramPuzzleState>(
            game.overworldTangramController)) {
      stage = game.registry
                  .get<shared::OverworldTangramPuzzleState>(
                      game.overworldTangramController)
                  .roleIsolationStage;
    }
    tangram_role_server::syncPieceCollisionLayer(game, ent, stage);

    LOG_DEBUG("[Tangram] Piece %u snapped to slot (use R to align rotation)\n",
           static_cast<unsigned>(piece.pieceId));
  }
}

void releasePlayersAfterExit(ServerGame& game) {
  auto& bodyInterface = game.physics.getBodyInterface();
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    uint8_t slot = 1;
    if (game.registry.all_of<shared::RenderInfo>(avatar)) {
      slot = game.registry.get<shared::RenderInfo>(avatar).playerSlot;
      if (slot < 1 || slot > 4) slot = 1;
    }
    const glm::vec3 spawn = maze_trigger::overworldSpawnPosition(game, slot);
    auto& pos = game.registry.get<shared::Position>(avatar);
    pos.x = spawn.x;
    pos.y = spawn.y;
    pos.z = spawn.z;

    if (game.registry.all_of<shared::Velocity>(avatar)) {
      auto& vel = game.registry.get<shared::Velocity>(avatar);
      vel.dx = vel.dy = vel.dz = 0.0f;
    }
    if (!game.registry.all_of<shared::PhysicsBody>(avatar)) continue;
    auto& pb = game.registry.get<shared::PhysicsBody>(avatar);
    JPH::BodyID body(pb.bodyId);
    if (!bodyInterface.IsAdded(body)) continue;
    bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                              JPH::EActivation::Activate);
    bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
    bodyInterface.SetAngularVelocity(body, JPH::Vec3::sZero());
  }
}

void tryRotateNearbyPiece(ServerGame& game) {
  constexpr float kReach = 2.8f;
  auto& bodyInterface = game.physics.getBodyInterface();

  auto findEntityByNetworkId = [&game](uint32_t networkId) -> entt::entity {
    if (networkId == 0) return entt::null;
    auto idView = game.registry.view<shared::Entity>();
    for (auto ent : idView) {
      if (idView.get<shared::Entity>(ent).id == networkId) return ent;
    }
    return entt::null;
  };

  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::PlayerInput, shared::Position>(avatar)) {
      continue;
    }
    const auto& input = game.registry.get<shared::PlayerInput>(avatar);
    if (!(input.keys_newly_pressed & KEY_ROTATE_PIECE)) continue;

    uint8_t stage = 0;
    if (game.registry.valid(game.overworldTangramController) &&
        game.registry.all_of<shared::OverworldTangramPuzzleState>(
            game.overworldTangramController)) {
      stage = game.registry
                  .get<shared::OverworldTangramPuzzleState>(
                      game.overworldTangramController)
                  .roleIsolationStage;
    }
    const uint8_t slot = tangram_role_server::playerSlotForAvatar(game, avatar);
    if (!shared::tangram_roles::canRotate(stage, slot)) continue;

    const auto& ppos = game.registry.get<shared::Position>(avatar);
    entt::entity best = entt::null;

    if (input.rotateTargetId != 0) {
      entt::entity candidate = findEntityByNetworkId(input.rotateTargetId);
      if (candidate != entt::null &&
          game.registry.all_of<shared::OverworldTangramPiece, shared::Position>(
              candidate)) {
        const auto& tpos = game.registry.get<shared::Position>(candidate);
        if (std::hypot(tpos.x - ppos.x, tpos.y - ppos.y) <= kReach) {
          best = candidate;
        }
      }
    }

    if (best == entt::null) {
      float bestDist = kReach;
      auto pieceView =
          game.registry.view<shared::OverworldTangramPiece, shared::Position,
                             shared::PhysicsBody>();
      for (auto ent : pieceView) {
        const auto& pos = pieceView.get<shared::Position>(ent);
        const float d = std::hypot(pos.x - ppos.x, pos.y - ppos.y);
        if (d < bestDist) {
          bestDist = d;
          best = ent;
        }
      }
    }
    if (best == entt::null) continue;

    auto& piece = game.registry.get<shared::TangramPiece>(best);
    const shared::tangram_puzzle::PieceDef* def =
        shared::tangram_puzzle::pieceDefForId(piece.pieceId);
    if (def == nullptr) continue;

    auto& pos = game.registry.get<shared::Position>(best);
    float relX = 0.0f;
    float relY = 0.0f;
    float targetRot = 0.0f;
    slotRelPose(game, *def, relX, relY, targetRot);
    const float qTarget =
        shared::tangram_puzzle::quantizeYawToRotateStep(targetRot);

    float newYaw = flattenedYaw(pos);
    if (piece.slotSnapped) {
      const float rel =
          shared::tangram_puzzle::normalizeYawRad(newYaw - qTarget);
      int step = static_cast<int>(
          std::lround(rel / shared::tangram_puzzle::kRotateStepRad));
      step = ((step % shared::tangram_puzzle::kRotateStepCount) +
              shared::tangram_puzzle::kRotateStepCount) %
             shared::tangram_puzzle::kRotateStepCount;
      const int nextStep =
          (step + 1) % shared::tangram_puzzle::kRotateStepCount;
      newYaw = shared::tangram_puzzle::normalizeYawRad(
          qTarget + static_cast<float>(nextStep) *
                        shared::tangram_puzzle::kRotateStepRad);
    } else {
      newYaw = shared::tangram_puzzle::quantizeYawToRotateStep(
          newYaw + shared::tangram_puzzle::kRotateStepRad);
    }

    const glm::quat q = flatQuatFromYaw(newYaw);
    if (piece.slotSnapped) {
      const glm::vec3 slotPos = slotSnapWorldPos(game, *def);
      if (shared::tangram_puzzle::yawMatchesTarget(newYaw, qTarget)) {
        applyPieceTransform(game, best, slotPos, flatQuatFromYaw(qTarget));
        LOG_DEBUG("[Tangram] Piece %u aligned with ghost slot\n",
               static_cast<unsigned>(piece.pieceId));
      } else {
        applyPieceTransform(game, best, slotPos, q);
      }
    } else {
      pos.qw = q.w;
      pos.qx = q.x;
      pos.qy = q.y;
      pos.qz = q.z;

      auto& pb = game.registry.get<shared::PhysicsBody>(best);
      JPH::BodyID body(pb.bodyId);
      if (!bodyInterface.IsAdded(body)) continue;
      bodyInterface.SetRotation(body, JPH::Quat(q.x, q.y, q.z, q.w),
                                JPH::EActivation::Activate);
      bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
      bodyInterface.SetAngularVelocity(body, JPH::Vec3::sZero());
    }

    tangram_role_server::syncPieceCollisionLayer(game, best, stage);
  }
}

void rollRandomPieceSpawns(ServerGame& game) {
  const shared::tangram::ArenaLayout& layout = game.tangramArena;

  if (shared::dev_spawn::spawnTangramPiecesNearSlots()) {
    for (int i = 0; i < shared::tangram_puzzle::kPieceCount; ++i) {
      const shared::tangram_puzzle::PieceDef& def =
          shared::tangram_puzzle::kPieces[i];
      float relX = 0.0f;
      float relY = 0.0f;
      float targetRot = 0.0f;
      slotRelPose(game, def, relX, relY, targetRot);
      const glm::vec3 slotPos = slotSnapWorldPos(game, def);
      glm::vec2 towardSlot(slotPos.x - layout.boardCenterX,
                           slotPos.y - layout.boardCenterY);
      const float len = std::hypot(towardSlot.x, towardSlot.y);
      if (len > 1e-3f) {
        towardSlot /= len;
      } else {
        towardSlot = {0.0f, -1.0f};
      }
      const float off = shared::dev_spawn::kTangramDevPieceOffsetFromSlotM;
      game.overworldFallFragmentSpawnXZ[static_cast<size_t>(i)] = {
          slotPos.x - towardSlot.x * off, slotPos.y - towardSlot.y * off};
    }
    LOG_DEBUG(
        "[DevSpawn] Pieces near ghost slots (%.1fm outside — short push to "
        "snap)\n",
        shared::dev_spawn::kTangramDevPieceOffsetFromSlotM);
    return;
  }

  const float tcx = layout.triggerCenterX;
  const float tcy = layout.triggerCenterY;
  const float bcx = layout.boardCenterX;
  const float bcy = layout.boardCenterY;

  // Fixed grid on the SOUTH half of the green trigger pad (away from goal
  // board).
  static constexpr glm::vec2
      kSpawnOffsets[shared::tangram_puzzle::kPieceCount] = {
          {-6.0f, -7.5f},  {-2.0f, -8.5f}, {2.0f, -8.5f},  {6.0f, -7.5f},
          {-4.0f, -10.5f}, {0.0f, -11.0f}, {4.0f, -10.5f},
      };

  for (int i = 0; i < shared::tangram_puzzle::kPieceCount; ++i) {
    glm::vec2 pos = kSpawnOffsets[i] + glm::vec2(tcx, tcy);
    if (!layout.isInsideTrigger(pos.x, pos.y) ||
        shared::tangram_puzzle::isInsideShapeGoalZone(pos.x, pos.y, bcx, bcy)) {
      pos = glm::vec2(tcx, tcy - layout.halfExtent * 0.55f);
    }
    game.overworldFallFragmentSpawnXZ[static_cast<size_t>(i)] = pos;
  }
}

bool allPiecesSolved(const ServerGame& game) {
  int count = 0;
  int aligned = 0;
  auto view = game.registry.view<shared::TangramPiece, shared::Position>();
  for (auto ent : view) {
    ++count;
    if (pieceAlignedWithSlot(game, ent)) {
      ++aligned;
    }
  }
  if (count != shared::tangram_puzzle::kPieceCount) {
    return false;
  }
  if (aligned == shared::tangram_puzzle::kPieceCount) {
    return true;
  }
  return false;
}

void lockAlignedPieces(ServerGame& game) {
  auto view = game.registry.view<shared::TangramPiece, shared::Position>();
  for (auto ent : view) {
    if (!pieceAlignedWithSlot(game, ent)) continue;
    auto& piece = view.get<shared::TangramPiece>(ent);
    piece.slotSnapped = true;
    holdSnappedPieceAtSlot(game, ent);
  }
}

void tryCompletePuzzle(ServerGame& game) {
  if (!game.overworldTangramActive || !allPiecesSolved(game)) return;

  entt::entity puzzleEnt = section_puzzle::findPuzzleForSection(
      game, shared::SectionSeasonMap::SPRING);
  if (puzzleEnt != entt::null &&
      game.registry.all_of<shared::PuzzleComponent>(puzzleEnt)) {
    game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase =
        shared::RunPhase::FINISHED;
  }

  const shared::tangram::ArenaLayout& arena = game.tangramArena;
  const float fragmentX = arena.triggerCenterX;
  const float fragmentY = arena.triggerCenterY;
  const float fragmentZ = arena.platformTopZ() + 2.0f;

  auto frags = game.registry.view<shared::FragmentComponent>();
  for (auto fe : frags) {
    if (frags.get<shared::FragmentComponent>(fe).season !=
        shared::SectionSeasonMap::SPRING)
      continue;
    if (game.registry.all_of<shared::Position>(fe)) {
      auto& fragPos = game.registry.get<shared::Position>(fe);
      fragPos.x = fragmentX;
      fragPos.y = fragmentY;
      fragPos.z = fragmentZ;
    }
    if (game.registry.all_of<shared::RenderInfo>(fe)) continue;
    game.registry.emplace<shared::RenderInfo>(fe, "fragment", 0.25f, 0.25f,
                                              0.25f);
    if (game.network != nullptr) {
      auto buf =
          serializeEntities(game.registry, game.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, {fe}, false);
      net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
    }
  }

  if (game.network != nullptr) {
    shared::SoundEventPacket soundPkt;
    soundPkt.soundId = static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED);
    soundPkt.volume = 1.0f;
    soundPkt.positional = false;
    net::broadcastPacket(game.network->getHost(), soundPkt);
  }

  LOG_DEBUG(
      "[Tangram] Puzzle complete — spring fragment spawned at trigger (%.1f, "
      "%.1f, %.1f). Pick it up to restore spring.\n",
      fragmentX, fragmentY, fragmentZ);
  endPuzzle(game, /*releasePlayers=*/false);
}

}  // namespace

bool isPieceCorrectlyPlaced(const ServerGame& game, entt::entity ent) {
  return pieceAlignedWithSlot(game, ent);
}

void clampPlayersToPlayArena(ServerGame& game) {
  const PlatformBounds b = fullPlatformBounds(game);
  const float minX = b.minX;
  const float maxX = b.maxX;
  const float minY = b.minY;
  const float maxY = b.maxY;
  const float minZ = b.minZ;
  const float maxZ = b.maxZ;

  auto& bodyInterface = game.physics.getBodyInterface();
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    entt::entity avatar = slots.overworld_avatar;
    if (!game.registry.valid(avatar) ||
        !game.registry.all_of<shared::Position>(avatar)) {
      continue;
    }
    auto& pos = game.registry.get<shared::Position>(avatar);
    bool clamped = false;
    if (pos.x < minX) {
      pos.x = minX;
      clamped = true;
    } else if (pos.x > maxX) {
      pos.x = maxX;
      clamped = true;
    }
    if (pos.y < minY) {
      pos.y = minY;
      clamped = true;
    } else if (pos.y > maxY) {
      pos.y = maxY;
      clamped = true;
    }
    if (pos.z < minZ) {
      pos.z = minZ;
      clamped = true;
    } else if (pos.z > maxZ) {
      pos.z = maxZ;
      clamped = true;
    }

    if (!game.registry.all_of<shared::PhysicsBody>(avatar)) continue;
    auto& pb = game.registry.get<shared::PhysicsBody>(avatar);
    JPH::BodyID body(pb.bodyId);
    if (!bodyInterface.IsAdded(body)) continue;

    if (clamped) {
      bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                                JPH::EActivation::Activate);
      bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
    }
  }
}

void initController(ServerGame& game) {
  auto [id, ent] = new_entity(game);
  (void)id;
  game.overworldTangramController = ent;
  game.registry.emplace<shared::OverworldTag>(ent);
  game.registry.emplace<shared::OverworldTangramPuzzleState>(ent);
}

bool isPuzzleActive(const ServerGame& game) {
  return game.overworldTangramActive;
}

void beginPuzzle(ServerGame& game) {
  if (game.overworldTangramActive) return;

  entt::entity puzzleEnt = section_puzzle::findPuzzleForSection(
      game, shared::SectionSeasonMap::SPRING);
  if (puzzleEnt != entt::null &&
      game.registry.all_of<shared::PuzzleComponent>(puzzleEnt)) {
    auto& puzzle = game.registry.get<shared::PuzzleComponent>(puzzleEnt);
    puzzle.phase = shared::RunPhase::INPROGRESS;
    puzzle.overworldKind = shared::OverworldPuzzleKind::Tangram;
  }

  rollRandomPieceSpawns(game);

  for (auto& slotEnt : game.overworldTangramGhostSlotEntities) {
    slotEnt = entt::null;
  }

  std::vector<entt::entity> spawned;
  auto& bodyInterface = game.physics.getBodyInterface();

  for (int i = 0; i < shared::tangram_puzzle::kPieceCount; ++i) {
    const shared::tangram_puzzle::PieceDef& def =
        shared::tangram_puzzle::kPieces[i];
    const glm::vec2& xz =
        game.overworldFallFragmentSpawnXZ[static_cast<size_t>(i)];
    const float pieceZ = def.scaleZ;
    const float z = game.tangramArena.pieceRestZ(pieceZ);
    const glm::vec3 pos(xz.x, xz.y, z);

    auto [fid, ent] = new_entity(game);
    (void)fid;
    game.registry.emplace<shared::OverworldTag>(ent);
    game.registry.emplace<shared::OverworldTangramPiece>(ent);
    game.registry.emplace<shared::TangramPiece>(ent, def.id, false);
    game.registry.emplace<shared::Position>(ent, pos.x, pos.y, pos.z, 1.0f,
                                            0.0f, 0.0f, 0.0f);
    {
      auto& ri = game.registry.emplace<shared::RenderInfo>(
          ent, def.modelName, def.scaleX, def.scaleY, pieceZ);
      ri.colorExempt = true;
    }
    game.registry.emplace<shared::Velocity>(ent, 0.0f, 0.0f, 0.0f);

    const uint8_t stage = shared::tangram_roles::kIsolationStage;
    const JPH::ObjectLayer pieceLayer =
        shared::tangram_roles::pushCollisionIsolation(stage) ? Layers::TANGRAM
                                                             : Layers::MOVING;
    JPH::BodyID body = game.physics.createTangramPieceBody(
        def.modelName, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(def.scaleX, def.scaleY, pieceZ), pieceLayer);
    game.registry.emplace<shared::PhysicsBody>(
        ent, body.GetIndexAndSequenceNumber());
    if (!bodyInterface.IsAdded(body)) {
      bodyInterface.AddBody(body, JPH::EActivation::Activate);
    }

    game.overworldFallFragmentEntities[static_cast<size_t>(i)] = ent;
    spawned.push_back(ent);

    float relX = 0.0f;
    float relY = 0.0f;
    float targetRot = 0.0f;
    slotRelPose(game, def, relX, relY, targetRot);
    const glm::vec3 ghostPos = slotSnapWorldPos(game, def);
    const glm::quat ghostRot = shared::tangram_puzzle::quatFromYawRad(
        shared::tangram_puzzle::quantizeYawToRotateStep(targetRot));
    auto [gid, ghostEnt] = new_entity(game);
    (void)gid;
    game.registry.emplace<shared::OverworldTag>(ghostEnt);
    game.registry.emplace<shared::TangramSlotGhost>(ghostEnt, def.id);
    game.registry.emplace<shared::Position>(ghostEnt, ghostPos.x, ghostPos.y,
                                            ghostPos.z, ghostRot.w, ghostRot.x,
                                            ghostRot.y, ghostRot.z);
    {
      auto& ghostRi = game.registry.emplace<shared::RenderInfo>(
          ghostEnt, shared::tangram_puzzle::ghostModelForId(def.id), def.scaleX,
          def.scaleY, shared::tangram_puzzle::kGhostSlotThickness);
      // Same as the pieces: puzzle objects opt out of the color-restoration
      // desaturation so the slot guides keep their tint instead of greying out.
      ghostRi.colorExempt = true;
    }
    game.overworldTangramGhostSlotEntities[static_cast<size_t>(i)] = ghostEnt;
    spawned.push_back(ghostEnt);
  }

  game.overworldTangramActive = true;
  const uint8_t isolationStage = shared::tangram_roles::kIsolationStage;
  setActiveFlag(game, true, isolationStage);
  tangram_role_server::syncCollisionRoles(game, isolationStage);
  clampPlayersToPlayArena(game);

  spawned.push_back(game.overworldTangramController);
  broadcastSpawn(game, spawned);

  LOG_DEBUG(
      "[Tangram] Started — push pieces onto the ghost swan | R rotate | "
      "snap all 7 + correct facing to win. Q to exit.\n");
  if (game.tangramSlotLayout.anyFromMap) {
    LOG_DEBUG("[Tangram] Slot poses from map (spring_tangram_slot_1..7)\n");
  } else {
    LOG_DEBUG(
        "[Tangram] Slot poses from tangram_puzzle_data.h (add Blender "
        "empties spring_tangram_slot_1..7 to override)\n");
  }
  LOG_DEBUG("[Tangram] %d ghost slots (one per piece, ids 1–7)\n",
         shared::tangram_puzzle::kPieceCount);
  if (shared::tangram_roles::rolesActive(isolationStage)) {
    LOG_DEBUG("[Tangram] Role isolation stage %u (0=off, 5=full split)\n",
           static_cast<unsigned>(isolationStage));
    if (shared::tangram_roles::rotateRestricted(isolationStage)) {
      LOG_DEBUG("[Tangram]   rotate: slot %u only\n",
             static_cast<unsigned>(shared::tangram_roles::kRotateSlot));
    }
    if (shared::tangram_roles::slotsRestricted(isolationStage)) {
      LOG_DEBUG("[Tangram]   ghost slots visible: slot %u only\n",
             static_cast<unsigned>(shared::tangram_roles::kSlotsSlot));
    }
    if (shared::tangram_roles::colorRestricted(isolationStage)) {
      LOG_DEBUG("[Tangram]   piece color visible: slot %u only (others grey)\n",
             static_cast<unsigned>(shared::tangram_roles::kColorSlot));
    }
  }
}

void endPuzzle(ServerGame& game, bool releasePlayers) {
  if (!game.overworldTangramActive) return;

  game.overworldTangramActive = false;
  tangram_role_server::revertCollisionRoles(game);
  setActiveFlag(game, false);

  auto despawnEnt = [&](entt::entity ent) {
    if (!game.registry.valid(ent)) return;
    if (game.network != nullptr) {
      shared::DespawnPacket pkt;
      pkt.type = shared::PacketType::DESPAWN_ENTITY;
      pkt.entityId = game.registry.get<shared::Entity>(ent).id;
      net::broadcastPacket(game.network->getHost(), pkt);
    }
    // PhysicsBody on_destroy hook removes the Jolt body — do not destroyBody
    // here.
    game.registry.destroy(ent);
  };

  for (auto& fragEnt : game.overworldFallFragmentEntities) {
    despawnEnt(fragEnt);
    fragEnt = entt::null;
  }
  for (auto& slotEnt : game.overworldTangramGhostSlotEntities) {
    despawnEnt(slotEnt);
    slotEnt = entt::null;
  }

  // Must leave the trigger pad before the puzzle can start again (prevents Q →
  // instant restart).
  game.overworldTangramTriggerArmed = false;
  game.overworldTangramFocusTimer = 0.0f;
  if (releasePlayers) {
    releasePlayersAfterExit(game);
    LOG_DEBUG("[Tangram] Puzzle ended — walk onto green pad to play again\n");
  } else {
    LOG_DEBUG(
        "[Tangram] Puzzle complete — players stay on board; pick up the "
        "fragment to restore spring\n");
  }
}

void clampPieceToArena(ServerGame& game, entt::entity ent) {
  if (!game.registry.all_of<shared::Position, shared::PhysicsBody,
                            shared::RenderInfo>(ent)) {
    return;
  }
  if (game.registry.all_of<shared::TangramPiece>(ent)) {
    flattenPieceRotation(game, ent);
    holdSnappedPieceAtSlot(game, ent);
    if (game.registry.get<shared::TangramPiece>(ent).slotSnapped) {
      return;
    }
  }
  auto& pos = game.registry.get<shared::Position>(ent);
  const auto& ri = game.registry.get<shared::RenderInfo>(ent);
  const float half = std::max(ri.sx, ri.sy) * 0.5f;
  const PlatformBounds b = fullPlatformBounds(game, 0.6f);
  const float minX = b.minX + half;
  const float maxX = b.maxX - half;
  const float minY = b.minY + half;
  const float maxY = b.maxY - half;
  const float z = game.tangramArena.pieceRestZ(ri.sz);

  bool moved = false;
  if (pos.x < minX) {
    pos.x = minX;
    moved = true;
  } else if (pos.x > maxX) {
    pos.x = maxX;
    moved = true;
  }
  if (pos.y < minY) {
    pos.y = minY;
    moved = true;
  } else if (pos.y > maxY) {
    pos.y = maxY;
    moved = true;
  }
  if (pos.z != z) {
    pos.z = z;
    moved = true;
  }

  if (!moved) return;
  auto& bodyInterface = game.physics.getBodyInterface();
  auto& pb = game.registry.get<shared::PhysicsBody>(ent);
  JPH::BodyID body(pb.bodyId);
  if (bodyInterface.IsAdded(body)) {
    bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                              JPH::EActivation::Activate);
    bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
  }
}

void updatePuzzle(ServerGame& game, float dt) {
  (void)dt;
  if (!game.overworldTangramActive) return;

  if (game.registry.valid(game.overworldTangramController) &&
      game.registry.all_of<shared::OverworldTangramPuzzleState>(
          game.overworldTangramController)) {
    const uint8_t stage = game.registry
                              .get<shared::OverworldTangramPuzzleState>(
                                  game.overworldTangramController)
                              .roleIsolationStage;
    tangram_role_server::syncCollisionRoles(game, stage);
  }

  // Press Q → exit tangram.
  auto inputView =
      game.registry.view<shared::PlayerInput, shared::OverworldTag>();
  for (auto ent : inputView) {
    if (game.registry.all_of<shared::OverworldTangramPiece>(ent)) continue;
    const auto& input = game.registry.get<shared::PlayerInput>(ent);
    if (input.keys_newly_pressed & KEY_EXIT_MINIGAME) {
      endPuzzle(game, /*releasePlayers=*/true);
      return;
    }
  }

  trySnapPiecesToSlots(game);
  tryRotateNearbyPiece(game);
  lockAlignedPieces(game);

  if (game.registry.valid(game.overworldTangramController) &&
      game.registry.all_of<shared::OverworldTangramPuzzleState>(
          game.overworldTangramController)) {
    const uint8_t stage = game.registry
                              .get<shared::OverworldTangramPuzzleState>(
                                  game.overworldTangramController)
                              .roleIsolationStage;
    tangram_role_server::syncCollisionRoles(game, stage);
  }

  tryCompletePuzzle(game);
}

}  // namespace tangram_puzzle
