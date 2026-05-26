#include "server/game/overworld_maze_puzzle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>

#include "server/game/maze.h"
#include "server/game/maze_generation.h"
#include "server/game/maze_spirit_control.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/input.h"
#include "shared/maze_preview.h"
#include "shared/net/packet_utils.h"

namespace overworld_maze_puzzle {
namespace {

constexpr int kMazeWidth = 8;
constexpr int kMazeHeight = 8;
constexpr uint32_t kMazeSeed = 12505;
constexpr float kPreviewTileSpacing = 0.18f;
constexpr float kMarkerDepthOffset = -0.30f;
constexpr glm::vec3 kStartMarkerScale(0.24f, 0.16f, 0.24f);
// Green + orange marker are larger than one tile; allow overlap reach.
constexpr float kGoalReachDistance = 0.32f;
constexpr float kPieceSpeed = 2.5f;

struct PreviewLayout {
  glm::vec3 startPos{};
  glm::vec3 goalPos{};
  int goalX = 0;
  int goalY = 0;
  float minX = 0.0f;
  float maxX = 0.0f;
  float minZ = 0.0f;
  float maxZ = 0.0f;
  float boardY = 0.0f;
  maze::MazeTileGrid tileGrid;
  float gridCenterX = 0.0f;
  float gridCenterZ = 0.0f;
  float gridXOffset = 0.0f;
  float gridYOffset = 0.0f;
};

PreviewLayout buildPreviewLayout() {
  const maze::MazeLayout layout =
      maze::GenerateMazeLayout(kMazeWidth, kMazeHeight, kMazeSeed);
  PreviewLayout out;
  out.tileGrid = maze::ConvertToTileGrid(layout);

  const glm::vec3 center(shared::maze_preview::kCenterX,
                         shared::maze_preview::kCenterY,
                         shared::maze_preview::kCenterZ);
  const float xOffset = (static_cast<float>(out.tileGrid.width) - 1.0f) * 0.5f;
  const float yOffset = (static_cast<float>(out.tileGrid.height) - 1.0f) * 0.5f;
  out.gridCenterX = center.x;
  out.gridCenterZ = center.z;
  out.gridXOffset = xOffset;
  out.gridYOffset = yOffset;

  auto tilePos = [&](int x, int y) {
    return glm::vec3(
        center.x + (static_cast<float>(x) - xOffset) * kPreviewTileSpacing,
        center.y + kMarkerDepthOffset,
        center.z + (yOffset - static_cast<float>(y)) * kPreviewTileSpacing);
  };

  const int startTileX = layout.startX * 2 + 1;
  const int startTileY = layout.startY * 2 + 1;
  out.goalX = layout.goalX;
  out.goalY = layout.goalY;
  const int goalTileX = layout.goalX * 2 + 1;
  const int goalTileY = layout.goalY * 2 + 1;
  out.startPos = tilePos(startTileX, startTileY);
  out.goalPos = tilePos(goalTileX, goalTileY);
  out.boardY = out.startPos.y;

  out.minX = center.x - xOffset * kPreviewTileSpacing;
  out.maxX = center.x + xOffset * kPreviewTileSpacing;
  out.minZ = center.z - yOffset * kPreviewTileSpacing;
  out.maxZ = center.z + yOffset * kPreviewTileSpacing;
  return out;
}

void installMazeGridOnGame(ServerGame& game, const PreviewLayout& layout) {
  game.overworldMazeGridWidth = layout.tileGrid.width;
  game.overworldMazeGridHeight = layout.tileGrid.height;
  game.overworldMazeGridTiles.resize(layout.tileGrid.tiles.size());
  for (size_t i = 0; i < layout.tileGrid.tiles.size(); ++i) {
    game.overworldMazeGridTiles[i] =
        layout.tileGrid.tiles[i] == maze::MazeTile::Floor ? 1 : 0;
  }
  game.overworldMazeGridCenterX = layout.gridCenterX;
  game.overworldMazeGridCenterZ = layout.gridCenterZ;
  game.overworldMazeGridXOffset = layout.gridXOffset;
  game.overworldMazeGridYOffset = layout.gridYOffset;
}

void worldToTile(const ServerGame& game, float worldX, float worldZ, int& tx,
                 int& ty) {
  const float relX = worldX - game.overworldMazeGridCenterX;
  const float relZ = worldZ - game.overworldMazeGridCenterZ;
  tx = static_cast<int>(
      std::lround(relX / kPreviewTileSpacing + game.overworldMazeGridXOffset));
  ty = static_cast<int>(
      std::lround(game.overworldMazeGridYOffset - relZ / kPreviewTileSpacing));
}

[[nodiscard]] bool isWalkableTile(const ServerGame& game, int tx, int ty) {
  if (game.overworldMazeGridWidth <= 0 || game.overworldMazeGridHeight <= 0) {
    return true;
  }
  if (tx < 0 || ty < 0 || tx >= game.overworldMazeGridWidth ||
      ty >= game.overworldMazeGridHeight) {
    return false;
  }
  const size_t idx = static_cast<size_t>(ty) *
                         static_cast<size_t>(game.overworldMazeGridWidth) +
                     static_cast<size_t>(tx);
  if (idx >= game.overworldMazeGridTiles.size()) return false;
  return game.overworldMazeGridTiles[idx] != 0;
}

[[nodiscard]] bool isWalkableWorld(const ServerGame& game, float worldX,
                                   float worldZ) {
  int tx = 0;
  int ty = 0;
  worldToTile(game, worldX, worldZ, tx, ty);
  return isWalkableTile(game, tx, ty);
}

[[nodiscard]] bool pieceFootprintWalkable(const ServerGame& game, float worldX,
                                          float worldZ) {
  constexpr float kRadius = 0.07f;
  const float offsets[5][2] = {{0.0f, 0.0f},
                               {kRadius, 0.0f},
                               {-kRadius, 0.0f},
                               {0.0f, kRadius},
                               {0.0f, -kRadius}};
  for (const auto& o : offsets) {
    if (!isWalkableWorld(game, worldX + o[0], worldZ + o[1])) return false;
  }
  return true;
}

void setPuzzleActiveFlag(ServerGame& game, bool active) {
  if (!game.registry.valid(game.overworldMazePuzzleController)) return;
  auto& state = game.registry.get<shared::OverworldMazePuzzleState>(
      game.overworldMazePuzzleController);
  state.active = active;
}

void broadcastSpawnEntities(ServerGame& game,
                            const std::vector<entt::entity>& entities) {
  if (entities.empty() || game.network == nullptr) return;
  auto buf =
      serializeEntities(game.registry, game.componentRegistry,
                        shared::PacketType::SPAWN_ENTITY, entities, false);
  net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
}

void broadcastDespawn(ServerGame& game, uint32_t entityId) {
  if (game.network == nullptr) return;
  shared::DespawnPacket pkt;
  pkt.type = shared::PacketType::DESPAWN_ENTITY;
  pkt.entityId = entityId;
  net::broadcastPacket(game.network->getHost(), pkt);
}

void applyOverworldDrive(ServerGame& game, entt::entity piece,
                         const maze_spirit_control::SpiritDrive& drive) {
  if (!game.registry.all_of<shared::PhysicsBody>(piece)) return;

  auto& bodyInterface = game.physics.getBodyInterface();
  auto& pb = game.registry.get<shared::PhysicsBody>(piece);
  JPH::BodyID body(pb.bodyId);
  if (!bodyInterface.IsAdded(body)) return;

  float vx = 0.0f;
  float vz = 0.0f;
  const float len = std::sqrt(drive.dx * drive.dx + drive.dy * drive.dy);
  if (len > 1e-5f) {
    vx = (drive.dx / len) * kPieceSpeed;
    vz = (drive.dy / len) * kPieceSpeed;
  }

  bodyInterface.SetLinearVelocity(body, JPH::Vec3(vx, 0.0f, vz));
  if (len <= 1e-5f) {
    bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
  }

  if (game.registry.all_of<shared::Velocity>(piece)) {
    auto& vel = game.registry.get<shared::Velocity>(piece);
    vel.dx = vx;
    vel.dy = 0.0f;
    vel.dz = vz;
  }
}

shared::MazeDirection padForJoinSlot(uint8_t slot) {
  switch (slot) {
    case 1:
      return shared::MazeDirection::UP;
    case 2:
      return shared::MazeDirection::DOWN;
    case 3:
      return shared::MazeDirection::LEFT;
    case 4:
      return shared::MazeDirection::RIGHT;
    default:
      return shared::MazeDirection::UP;
  }
}

void claimOverworldPadsForActivePlayers(ServerGame& game) {
  for (auto& kv : game.active_players) {
    entt::entity avatar = kv.second.overworld_avatar;
    if (avatar == entt::null || !game.registry.valid(avatar)) continue;
    if (!game.registry.all_of<shared::RenderInfo>(avatar)) continue;
    uint8_t slot = game.registry.get<shared::RenderInfo>(avatar).playerSlot;
    if (slot < 1 || slot > 4) slot = 1;
    const auto pad = padForJoinSlot(slot);
    game.registry.emplace_or_replace<shared::MazePadBinding>(avatar, pad);
  }
}

void freezeOverworldPlayerAvatars(ServerGame& game) {
  auto& bodyInterface = game.physics.getBodyInterface();
  auto view = game.registry.view<shared::OverworldTag, shared::PhysicsBody,
                                 shared::PlayerInput>();
  for (auto ent : view) {
    if (game.registry.all_of<shared::OverworldMazePiece>(ent)) continue;
    auto& pb = view.get<shared::PhysicsBody>(ent);
    JPH::BodyID body(pb.bodyId);
    if (!bodyInterface.IsAdded(body)) continue;
    bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
    bodyInterface.SetAngularVelocity(body, JPH::Vec3::sZero());
  }
}

}  // namespace

namespace {

[[nodiscard]] bool pieceOnGoalTile(const ServerGame& game, float worldX,
                                   float worldZ) {
  int tx = 0;
  int ty = 0;
  worldToTile(game, worldX, worldZ, tx, ty);
  if (tx == game.overworldMazeGoalTileX && ty == game.overworldMazeGoalTileY) {
    return true;
  }

  const float dx = worldX - game.overworldMazePreviewGoal.x;
  const float dz = worldZ - game.overworldMazePreviewGoal.z;
  return std::sqrt(dx * dx + dz * dz) <= kGoalReachDistance;
}

}  // namespace

void initOverworldMazePuzzleController(ServerGame& game) {
  auto [id, ent] = new_entity(game);
  (void)id;
  game.registry.emplace<shared::OverworldTag>(ent);
  game.registry.emplace<shared::Position>(
      ent, shared::maze_preview::kBoardCenterX,
      shared::maze_preview::kBoardCenterY, shared::maze_preview::kBoardCenterZ,
      1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::OverworldMazePuzzleState>(ent, false);
  game.overworldMazePuzzleController = ent;
}

bool isPuzzleActive(const ServerGame& game) {
  return game.overworldMazePuzzleActive;
}

void beginPuzzle(ServerGame& game) {
  if (game.overworldMazePuzzleActive) return;

  const PreviewLayout layout = buildPreviewLayout();
  game.overworldMazePreviewGoal = layout.goalPos;
  game.overworldMazePreviewBoardY = layout.boardY;
  game.overworldMazePreviewMinX = layout.minX;
  game.overworldMazePreviewMaxX = layout.maxX;
  game.overworldMazePreviewMinZ = layout.minZ;
  game.overworldMazePreviewMaxZ = layout.maxZ;
  installMazeGridOnGame(game, layout);
  game.overworldMazeLastValidX = layout.startPos.x;
  game.overworldMazeLastValidZ = layout.startPos.z;
  game.overworldMazeGoalTileX = layout.goalX * 2 + 1;
  game.overworldMazeGoalTileY = layout.goalY * 2 + 1;
  game.overworldMazeReachGoalPending = false;

  auto [pieceId, piece] = new_entity(game);
  (void)pieceId;
  game.registry.emplace<shared::OverworldTag>(piece);
  game.registry.emplace<shared::OverworldMazePiece>(piece);
  game.registry.emplace<shared::Position>(piece, layout.startPos.x,
                                          layout.startPos.y, layout.startPos.z,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::Velocity>(piece, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(
      piece, "start_cube", kStartMarkerScale.x, kStartMarkerScale.y,
      kStartMarkerScale.z);

  JPH::BodyID body = game.physics.createMazeBoardPieceBody(
      "start_cube", layout.startPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
      kStartMarkerScale);
  game.registry.emplace<shared::PhysicsBody>(piece,
                                             body.GetIndexAndSequenceNumber());

  auto& bodyInterface = game.physics.getBodyInterface();
  if (!bodyInterface.IsAdded(body)) {
    bodyInterface.AddBody(body, JPH::EActivation::Activate);
  }

  game.overworldMazePieceEntity = piece;
  game.overworldMazePuzzleActive = true;
  setPuzzleActiveFlag(game, true);
  EnterMazePuzzle(game);
  claimOverworldPadsForActivePlayers(game);

  broadcastSpawnEntities(game, {piece, game.overworldMazePuzzleController});

  printf(
      "[OverworldMaze] Puzzle started on preview board at (%.2f, %.2f, "
      "%.2f)\n",
      layout.startPos.x, layout.startPos.y, layout.startPos.z);
}

void endPuzzle(ServerGame& game) {
  if (!game.overworldMazePuzzleActive) return;

  game.overworldMazePuzzleActive = false;
  setPuzzleActiveFlag(game, false);
  ExitMazePuzzle(game);
  // Players can move with WASD again; re-arm only after everyone leaves the
  // pad.
  game.overworldMazeTriggerArmed = false;
  game.overworldMazeGridTiles.clear();
  game.overworldMazeGridWidth = 0;
  game.overworldMazeGridHeight = 0;

  if (game.registry.valid(game.overworldMazePieceEntity)) {
    const uint32_t eid =
        game.registry.get<shared::Entity>(game.overworldMazePieceEntity).id;
    broadcastDespawn(game, eid);
    game.registry.destroy(game.overworldMazePieceEntity);
    game.overworldMazePieceEntity = entt::null;
  }

  for (auto& kv : game.active_players) {
    entt::entity avatar = kv.second.overworld_avatar;
    if (avatar != entt::null && game.registry.valid(avatar) &&
        game.registry.all_of<shared::MazePadBinding>(avatar)) {
      game.registry.remove<shared::MazePadBinding>(avatar);
    }
  }

  broadcastSpawnEntities(game, {game.overworldMazePuzzleController});

  printf("[OverworldMaze] Puzzle ended\n");
}

void updatePuzzle(ServerGame& game, float dt) {
  (void)dt;

  if (!game.overworldMazePuzzleActive) return;

  const entt::entity piece = game.overworldMazePieceEntity;
  if (piece == entt::null || !game.registry.valid(piece)) {
    endPuzzle(game);
    return;
  }

  auto inputView =
      game.registry.view<shared::PlayerInput, shared::OverworldTag>();
  for (auto ent : inputView) {
    if (game.registry.all_of<shared::OverworldMazePiece>(ent)) continue;
    const auto& input = game.registry.get<shared::PlayerInput>(ent);
    if (input.keys_newly_pressed & KEY_EXIT_MINIGAME) {
      endPuzzle(game);
      return;
    }
  }

  freezeOverworldPlayerAvatars(game);

  const maze_spirit_control::SpiritDrive drive =
      maze_spirit_control::collectSpiritDriveFromOverworldPlayers(game);
  applyOverworldDrive(game, piece, drive);
}

void clampPieceToBoard(ServerGame& game) {
  const entt::entity piece = game.overworldMazePieceEntity;
  if (piece == entt::null || !game.registry.valid(piece) ||
      !game.registry.all_of<shared::Position, shared::PhysicsBody>(piece)) {
    return;
  }

  auto& pos = game.registry.get<shared::Position>(piece);
  auto& pb = game.registry.get<shared::PhysicsBody>(piece);
  auto& bodyInterface = game.physics.getBodyInterface();
  JPH::BodyID body(pb.bodyId);
  if (!bodyInterface.IsAdded(body)) return;

  bool bounced = false;
  if (pos.x < game.overworldMazePreviewMinX) {
    pos.x = game.overworldMazePreviewMinX;
    bounced = true;
  } else if (pos.x > game.overworldMazePreviewMaxX) {
    pos.x = game.overworldMazePreviewMaxX;
    bounced = true;
  }
  if (pos.z < game.overworldMazePreviewMinZ) {
    pos.z = game.overworldMazePreviewMinZ;
    bounced = true;
  } else if (pos.z > game.overworldMazePreviewMaxZ) {
    pos.z = game.overworldMazePreviewMaxZ;
    bounced = true;
  }
  pos.y = game.overworldMazePreviewBoardY;

  // Check goal before wall rejection so a blocked approach still counts.
  if (pieceOnGoalTile(game, pos.x, pos.z)) {
    game.overworldMazeReachGoalPending = true;
  }

  if (!pieceFootprintWalkable(game, pos.x, pos.z)) {
    pos.x = game.overworldMazeLastValidX;
    pos.z = game.overworldMazeLastValidZ;
    bounced = true;
  } else {
    game.overworldMazeLastValidX = pos.x;
    game.overworldMazeLastValidZ = pos.z;
  }

  bodyInterface.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                            JPH::EActivation::Activate);

  if (bounced) {
    bodyInterface.SetLinearVelocity(body, JPH::Vec3::sZero());
    if (game.registry.all_of<shared::Velocity>(piece)) {
      auto& vel = game.registry.get<shared::Velocity>(piece);
      vel.dx = vel.dy = vel.dz = 0.0f;
    }
  }
}

void tryCompleteOnGoal(ServerGame& game) {
  if (!game.overworldMazePuzzleActive || !game.overworldMazeReachGoalPending) {
    return;
  }
  game.overworldMazeReachGoalPending = false;

  printf("[OverworldMaze] Green piece reached goal — auto exit\n");
  CollectMazeFragment(game);
  endPuzzle(game);
}

}  // namespace overworld_maze_puzzle
