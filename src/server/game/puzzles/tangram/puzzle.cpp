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
#include "shared/input.h"
#include "shared/net/packet_utils.h"
#include "shared/puzzles/tangram/defaults.h"
#include "shared/puzzles/tangram/puzzle_data.h"
#include "shared/puzzles/tangram/roles.h"
#include "shared/puzzles/tangram/slot_validate.h"

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
  rotRad = def.targetRotRad;
}

}  // namespace

namespace tangram_puzzle {

void endPuzzle(ServerGame& game);

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

  const glm::vec3 slotPos = slotSnapWorldPos(game, *def);
  const float reach = shared::tangram_puzzle::snapRadiusForPiece(*def) * 1.15f;
  if (std::hypot(pos.x - slotPos.x, pos.y - slotPos.y) > reach) {
    return false;
  }

  const float yaw = yawFromPosition(pos);
  return shared::tangram_puzzle::yawMatchesTarget(yaw, targetRot);
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
  if (pos.x == slotPos.x && pos.y == slotPos.y && pos.z == slotPos.z) return;

  const glm::quat rot(pos.qw, pos.qx, pos.qy, pos.qz);
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
    const glm::vec3 slotPos = slotSnapWorldPos(game, *def);
    const float reach = shared::tangram_puzzle::snapRadiusForPiece(*def);
    if (std::hypot(pos.x - slotPos.x, pos.y - slotPos.y) > reach) continue;

    piece.slotSnapped = true;
    // Lock XY to slot; random facing — use R to align each piece.
    const float snapYaw = static_cast<float>(stepDist(tangramSnapRng())) *
                          shared::tangram_puzzle::kRotateStepRad;
    const glm::quat rot = shared::tangram_puzzle::quatFromYawRad(snapYaw);
    applyPieceTransform(game, ent, slotPos, rot);

    printf("[Tangram] Piece %u snapped to slot (use R to align rotation)\n",
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

    auto& pos = game.registry.get<shared::Position>(best);
    const float yaw = yawFromPosition(pos);
    const float newYaw = yaw + shared::tangram_puzzle::kRotateStepRad;
    glm::quat q = glm::angleAxis(newYaw, glm::vec3(0.0f, 0.0f, 1.0f));
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
}

void rollRandomPieceSpawns(ServerGame& game) {
  const shared::tangram::ArenaLayout& layout = game.tangramArena;
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
  auto view = game.registry.view<shared::TangramPiece, shared::Position>();
  for (auto ent : view) {
    if (!pieceAlignedWithSlot(game, ent)) return false;
    ++count;
  }
  return count == shared::tangram_puzzle::kPieceCount;
}

void tryCompletePuzzle(ServerGame& game) {
  if (!game.overworldTangramActive || !allPiecesSolved(game)) return;

  entt::entity puzzleEnt = section_puzzle::findPuzzleForSection(
      game, shared::SectionSeasonMap::FALL);
  if (puzzleEnt != entt::null &&
      game.registry.all_of<shared::PuzzleComponent>(puzzleEnt)) {
    game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase =
        shared::RunPhase::FINISHED;
  }
  if (!section_puzzle::isSectionCompleted(game,
                                          shared::SectionSeasonMap::FALL)) {
    section_puzzle::completeSection(game, shared::SectionSeasonMap::FALL);
  }
  printf(
      "[Tangram] All pieces in slots with correct rotation — puzzle "
      "finished\n");
  endPuzzle(game);
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
      game, shared::SectionSeasonMap::FALL);
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
    game.registry.emplace<shared::RenderInfo>(ent, def.modelName, def.scaleX,
                                              def.scaleY, pieceZ);
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
    const float topZ = game.tangramArena.platformTopZ();
    const glm::vec3 ghostPos(
        game.tangramArena.boardCenterX + relX,
        game.tangramArena.boardCenterY + relY,
        topZ + shared::tangram_puzzle::kGhostSlotThickness * 0.5f + 0.02f);
    const glm::quat ghostRot =
        shared::tangram_puzzle::quatFromYawRad(targetRot);
    auto [gid, ghostEnt] = new_entity(game);
    (void)gid;
    game.registry.emplace<shared::OverworldTag>(ghostEnt);
    game.registry.emplace<shared::TangramSlotGhost>(ghostEnt, def.id);
    game.registry.emplace<shared::Position>(ghostEnt, ghostPos.x, ghostPos.y,
                                            ghostPos.z, ghostRot.w, ghostRot.x,
                                            ghostRot.y, ghostRot.z);
    constexpr float kGhostScale = 0.97f;
    game.registry.emplace<shared::RenderInfo>(
        ghostEnt, shared::tangram_puzzle::ghostModelForId(def.id),
        def.scaleX * kGhostScale, def.scaleY * kGhostScale,
        shared::tangram_puzzle::kGhostSlotThickness);
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

  printf(
      "[Tangram] Started — push pieces onto the ghost swan | R rotate | "
      "snap all 7 + correct facing to win. Q to exit.\n");
  if (game.tangramSlotLayout.anyFromMap) {
    printf("[Tangram] Slot poses from map (spring_tangram_slot_1..7)\n");
  } else {
    printf(
        "[Tangram] Slot poses from tangram_puzzle_data.h (add Blender "
        "empties spring_tangram_slot_1..7 to override)\n");
  }
  printf("[Tangram] %d ghost slots (one per piece, ids 1–7)\n",
         shared::tangram_puzzle::kPieceCount);
  if (shared::tangram_roles::rolesActive(isolationStage)) {
    printf("[Tangram] Role isolation stage %u (0=off, 5=full split)\n",
           static_cast<unsigned>(isolationStage));
    if (shared::tangram_roles::rotateRestricted(isolationStage)) {
      printf("[Tangram]   rotate: slot %u only\n",
             static_cast<unsigned>(shared::tangram_roles::kRotateSlot));
    }
    if (shared::tangram_roles::slotsRestricted(isolationStage)) {
      printf("[Tangram]   ghost slots visible: slot %u only\n",
             static_cast<unsigned>(shared::tangram_roles::kSlotsSlot));
    }
    if (shared::tangram_roles::colorRestricted(isolationStage)) {
      printf("[Tangram]   piece color visible: slot %u only (others grey)\n",
             static_cast<unsigned>(shared::tangram_roles::kColorSlot));
    }
  }
}

void endPuzzle(ServerGame& game) {
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
  releasePlayersAfterExit(game);
  printf("[Tangram] Puzzle ended — walk onto green pad to play again\n");
}

void clampPieceToArena(ServerGame& game, entt::entity ent) {
  if (!game.registry.all_of<shared::Position, shared::PhysicsBody,
                            shared::RenderInfo>(ent)) {
    return;
  }
  if (game.registry.all_of<shared::TangramPiece>(ent)) {
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
      endPuzzle(game);
      return;
    }
  }

  trySnapPiecesToSlots(game);
  tryRotateNearbyPiece(game);
  tryCompletePuzzle(game);
}

}  // namespace tangram_puzzle
