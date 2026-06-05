#include "server/game/puzzles/maze/layout_editor.h"

#include <cstdio>
#include <glm/glm.hpp>
#include <vector>

#include "server/game/maze_generation.h"
#include "server/game/puzzles/maze/puzzle.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/log.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"

namespace maze_layout_editor {
namespace {

constexpr int kMazeWidth = 8;
constexpr int kMazeHeight = 8;
constexpr uint32_t kMazeSeed = 12505;
constexpr float kPreviewTileSpacing = 0.18f;

std::vector<entt::entity> spawnDescBatch(
    ServerGame& game, const std::vector<StaticEntityDesc>& descs) {
  std::vector<entt::entity> created;
  created.reserve(descs.size());
  for (const StaticEntityDesc& d : descs) {
    auto [id, entity] = new_entity(game);
    (void)id;
    game.registry.emplace<shared::Position>(
        entity, d.position.x, d.position.y, d.position.z, d.rotation.w,
        d.rotation.x, d.rotation.y, d.rotation.z);
    game.registry.emplace<shared::OverworldTag>(entity);
    game.registry.emplace<shared::MazeLayoutVisual>(entity);
    if (!d.modelName.empty()) {
      auto& ri = game.registry.emplace<shared::RenderInfo>(
          entity, d.modelName, d.scale.x, d.scale.y, d.scale.z);
      ri.colorExempt = true;
    }
    if (d.collision == CollisionShape::None) {
      created.push_back(entity);
      continue;
    }

    JPH::ShapeRefC shape;
    if (d.collision == CollisionShape::Mesh) {
      shape = game.physics.meshShapeForAsset(d.modelName, d.scale);
      if (!shape) {
        shape = game.physics.boxShapeForAsset(d.modelName, d.scale);
      }
    } else {
      shape = game.physics.boxShapeForAsset(d.modelName, d.scale);
    }
    if (shape) {
      JPH::BodyID bodyId =
          game.physics.createStaticBody(shape, d.position, d.rotation);
      game.registry.emplace<shared::PhysicsBody>(
          entity, bodyId.GetIndexAndSequenceNumber());
    }
    created.push_back(entity);
  }
  return created;
}

void broadcastSpawnEntities(ServerGame& game,
                            const std::vector<entt::entity>& entities) {
  if (entities.empty() || game.network == nullptr) return;
  auto buf =
      serializeEntities(game.registry, game.componentRegistry,
                        shared::PacketType::SPAWN_ENTITY, entities, false);
  net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
}

}  // namespace

std::vector<StaticEntityDesc> buildPreviewEntities(
    const shared::maze_layout::Config& layout) {
  const maze::MazeLayout mazeLayout =
      maze::GenerateMazeLayout(kMazeWidth, kMazeHeight, kMazeSeed);
  const maze::MazeTileGrid tileGrid = maze::ConvertToTileGrid(mazeLayout);

  const float baseZ = layout.boardWallBaseZ();
  const float faceY = layout.boardFaceY();
  constexpr float kFloorDepthOffset = 0.02f;
  constexpr float kWallDepthOffset = 0.16f;
  constexpr glm::vec3 kFloorScale(0.18f, 0.02f, 0.18f);
  constexpr glm::vec3 kWallScale(0.18f, 0.30f, 0.18f);
  constexpr float kMarkerDepthOffset = 0.08f;
  constexpr glm::vec3 kGoalMarkerScale(0.28f, 0.16f, 0.28f);

  const float xOffset = (static_cast<float>(tileGrid.width) - 1.0f) * 0.5f;
  const float yOffset = (static_cast<float>(tileGrid.height) - 1.0f) * 0.5f;
  const float centerX = layout.boardCenterX;

  auto previewPosition = [&](int x, int y, float depthAlongY) {
    return glm::vec3(
        centerX + (static_cast<float>(x) - xOffset) * kPreviewTileSpacing,
        faceY + depthAlongY,
        baseZ + (static_cast<float>(y) - yOffset) * kPreviewTileSpacing);
  };

  std::vector<StaticEntityDesc> entities;
  for (int y = 0; y < tileGrid.height; ++y) {
    for (int x = 0; x < tileGrid.width; ++x) {
      const bool isWall = tileGrid.Tile(x, y) == maze::MazeTile::Wall;
      if (isWall) {
        entities.push_back(StaticEntityDesc{
            .position = previewPosition(x, y, kWallDepthOffset),
            .modelName = "light_cube",
            .scale = kWallScale,
            .collision = CollisionShape::None,
        });
      } else {
        entities.push_back(StaticEntityDesc{
            .position = previewPosition(x, y, kFloorDepthOffset),
            .modelName = "cube",
            .scale = kFloorScale,
            .collision = CollisionShape::None,
        });
      }
    }
  }

  const int goalTileX = mazeLayout.goalX * 2 + 1;
  const int goalTileY = mazeLayout.goalY * 2 + 1;
  entities.push_back(StaticEntityDesc{
      .position = previewPosition(goalTileX, goalTileY, kMarkerDepthOffset),
      .modelName = "goal_cube",
      .scale = kGoalMarkerScale,
      .collision = CollisionShape::None,
  });
  return entities;
}

std::vector<StaticEntityDesc> buildTriggerMarkerEntities(
    const shared::maze_layout::Config& layout) {
  (void)layout;
  // Trigger pad outline comes from landscape.glb (e.g. Maze fence*). Logic uses
  // maze_trigger Empty coords only — no procedural maze_trigger_cube markers.
  return {};
}

void despawnLayoutVisuals(ServerGame& game) {
  std::vector<entt::entity> toRemove;
  auto view = game.registry.view<shared::MazeLayoutVisual, shared::Entity>();
  for (auto ent : view) {
    toRemove.push_back(ent);
  }

  for (auto ent : toRemove) {
    const uint32_t eid = game.registry.get<shared::Entity>(ent).id;
    if (game.network != nullptr) {
      shared::DespawnPacket pkt;
      pkt.type = shared::PacketType::DESPAWN_ENTITY;
      pkt.entityId = eid;
      net::broadcastPacket(game.network->getHost(), pkt);
    }
    game.registry.destroy(ent);
  }
}

void spawnLayoutVisuals(ServerGame& game) {
  std::vector<entt::entity> spawned;
  auto append = [&](const std::vector<StaticEntityDesc>& descs) {
    auto created = spawnDescBatch(game, descs);
    spawned.insert(spawned.end(), created.begin(), created.end());
  };
  append(buildPreviewEntities(game.mazeLayout));
  append(buildTriggerMarkerEntities(game.mazeLayout));
}

void applyLayout(ServerGame& game, shared::maze_layout::Config layout) {
  if (maze_puzzle::isPuzzleActive(game)) {
    LOG_DEBUG("[MazeLayout] Cannot apply while puzzle is active.\n");
    return;
  }
  if (layout.autoBoardFromTrigger) {
    layout.syncBoardFromTrigger();
  }
  layout.resolveBoardPlacement();
  game.mazeLayout = layout;
  despawnLayoutVisuals(game);
  spawnLayoutVisuals(game);
  std::vector<entt::entity> spawned;
  auto view = game.registry.view<shared::MazeLayoutVisual, shared::Entity>();
  for (auto ent : view) {
    spawned.push_back(ent);
  }
  broadcastSpawnEntities(game, spawned);
  printLayoutSnippet(game.mazeLayout);
  LOG_DEBUG(
      "[MazeLayout] Applied trigger (%.2f, %.2f) board (%.2f, %.2f, %.2f)\n",
      game.mazeLayout.triggerCenterX, game.mazeLayout.triggerCenterY,
      game.mazeLayout.boardCenterX, game.mazeLayout.boardCenterY,
      game.mazeLayout.boardCenterZ);
}

void printLayoutSnippet(const shared::maze_layout::Config& layout) {
  LOG_DEBUG(
      "--- paste into maze_preview.h (trigger/board) ---\n"
      "constexpr float kTriggerCenterX = %.3ff;\n"
      "constexpr float kTriggerCenterY = %.3ff;\n"
      "constexpr float kTriggerCenterZ = %.3ff;\n"
      "constexpr float kHalfExtent = %.3ff;\n"
      "constexpr float kMazeGap = %.3ff;\n"
      "constexpr float kBoardCenterX = %.3ff;\n"
      "constexpr float kBoardCenterY = %.3ff;\n"
      "constexpr float kBoardCenterZ = %.3ff;\n"
      "------------------------------------------------\n",
      layout.triggerCenterX, layout.triggerCenterY, layout.triggerCenterZ,
      layout.halfExtent, layout.mazeGap, layout.boardCenterX,
      layout.boardCenterY, layout.boardCenterZ);
}

}  // namespace maze_layout_editor
