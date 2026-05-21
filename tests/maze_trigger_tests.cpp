#include <gtest/gtest.h>

#include <enet/enet.h>

#include "server/game/maze_trigger.h"
#include "server/server_game.h"
#include "shared/component_registry.h"
#include "shared/components.h"

namespace {

entt::entity makeOverworldAvatar(ServerGame& game, uint32_t entityId, float x,
                                 float y) {
  auto ent = game.registry.create();
  game.registry.emplace<shared::Entity>(ent, entityId);
  game.registry.emplace<shared::Position>(ent, x, y, 0.0f, 1.0f, 0.0f, 0.0f,
                                            0.0f);
  return ent;
}

void addPlayerSlot(ServerGame& game, ENetPeer* peerKey, float x, float y) {
  PlayerAvatars slots;
  slots.overworld_avatar = makeOverworldAvatar(game, game.nextEntityId++, x,
                                               y);
  slots.maze_avatar = entt::null;
  game.active_players[peerKey] = slots;
}

}  // namespace

TEST(MazeTrigger, CenterInsideRegion) {
  shared::Position p{maze_trigger::kCenterX, maze_trigger::kCenterY, 0.0f,
                     1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(p));
}

TEST(MazeTrigger, ZIgnored) {
  shared::Position p{maze_trigger::kCenterX, maze_trigger::kCenterY, -999.0f,
                     1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(p));
}

TEST(MazeTrigger, OutsidePositiveX) {
  shared::Position p{maze_trigger::kCenterX + maze_trigger::kHalfExtent + 0.1f,
                     maze_trigger::kCenterY, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_FALSE(maze_trigger::isInsideMazeTriggerRegion(p));
}

TEST(MazeTrigger, OnBoundaryInclusive) {
  shared::Position p{maze_trigger::kCenterX + maze_trigger::kHalfExtent,
                     maze_trigger::kCenterY, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(p));
}

TEST(MazeTrigger, AllPlayersRequiresExactlyFourClients) {
  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  initServerGame(game);

  static char peerStub[4];
  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));

  for (int i = 0; i < 3; ++i) {
    addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[i]),
                  maze_trigger::kCenterX, maze_trigger::kCenterY);
  }
  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));

  addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[3]),
                maze_trigger::kCenterX, maze_trigger::kCenterY);
  EXPECT_TRUE(maze_trigger::allActivePlayersInMazeTrigger(game));
}

TEST(MazeTrigger, AllPlayersFailsIfOneOutside) {
  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  initServerGame(game);

  static char peerStub[4];
  for (int i = 0; i < 3; ++i) {
    addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[i]),
                  maze_trigger::kCenterX, maze_trigger::kCenterY);
  }
  addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[3]), 0.0f, 0.0f);

  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));
}

TEST(MazeTrigger, MarkerEntitiesOutlineSquare) {
  const auto entities = maze_trigger::buildMazeTriggerMarkerEntities();
  EXPECT_EQ(entities.size(), 20u);

  const float minX = maze_trigger::kCenterX - maze_trigger::kHalfExtent;
  const float minY = maze_trigger::kCenterY - maze_trigger::kHalfExtent;
  EXPECT_EQ(entities[0].modelName, "goal_cube");
  EXPECT_FLOAT_EQ(entities[0].position.x, minX);
  EXPECT_FLOAT_EQ(entities[0].position.y, minY);
}
