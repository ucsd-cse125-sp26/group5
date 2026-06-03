#include <gtest/gtest.h>

#include <enet/enet.h>

#include "server/game/puzzles/maze/trigger.h"
#include "server/server_game.h"
#include "shared/component_registry.h"
#include "shared/components.h"
#include "shared/puzzles/maze/layout.h"

namespace {

const shared::maze_layout::Config kLayout =
    shared::maze_layout::Config::defaults();

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
  shared::Position p{kLayout.triggerCenterX, kLayout.triggerCenterY, 0.0f,
                     1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(p, kLayout));
}

TEST(MazeTrigger, ZIgnored) {
  shared::Position p{kLayout.triggerCenterX, kLayout.triggerCenterY, -999.0f,
                     1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(p, kLayout));
}

TEST(MazeTrigger, OutsidePositiveX) {
  shared::Position p{kLayout.triggerCenterX + kLayout.halfExtent + 0.1f,
                     kLayout.triggerCenterY, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_FALSE(maze_trigger::isInsideMazeTriggerRegion(p, kLayout));
}

TEST(MazeTrigger, OnBoundaryInclusive) {
  shared::Position p{kLayout.triggerCenterX + kLayout.halfExtent,
                     kLayout.triggerCenterY, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(p, kLayout));
}

TEST(MazeTrigger, AllPlayersRequiresFourClientsInTrigger) {
  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  initServerGame(game);

  static char peerStub[4];
  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));

  addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[0]),
                kLayout.triggerCenterX, kLayout.triggerCenterY);
  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));

  for (int i = 1; i < 3; ++i) {
    addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[i]),
                  kLayout.triggerCenterX, kLayout.triggerCenterY);
  }
  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));

  addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[3]),
                kLayout.triggerCenterX, kLayout.triggerCenterY);
  EXPECT_TRUE(maze_trigger::allActivePlayersInMazeTrigger(game));
}

TEST(MazeTrigger, AllPlayersFailsIfOneOutside) {
  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  initServerGame(game);

  static char peerStub[4];
  for (int i = 0; i < 3; ++i) {
    addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[i]),
                  kLayout.triggerCenterX, kLayout.triggerCenterY);
  }
  addPlayerSlot(game, reinterpret_cast<ENetPeer*>(&peerStub[3]), 0.0f, 0.0f);

  EXPECT_FALSE(maze_trigger::allActivePlayersInMazeTrigger(game));
}

TEST(MazeTrigger, MarkerEntitiesOutlineSquare) {
  const auto entities = maze_trigger::buildMazeTriggerMarkerEntities(kLayout);
  EXPECT_TRUE(entities.empty());
}
