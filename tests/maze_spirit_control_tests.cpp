#include <gtest/gtest.h>

#include <cmath>

#include "server/game/maze.h"
#include "server/game/maze_spirit_control.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/input.h"

namespace {

entt::entity makeMazePlayer(ServerGame& game, uint8_t slot) {
  auto ent = game.registry.create();
  game.registry.emplace<shared::Entity>(ent, game.nextEntityId++);
  game.registry.emplace<shared::MazeTag>(ent);
  game.registry.emplace<shared::PlayerInput>(ent, InputKeys(0), InputKeys(0),
                                             InputKeys(0), 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(ent, "bear", 0.5f, 0.5f, 0.5f);
  game.registry.get<shared::RenderInfo>(ent).playerSlot = slot;
  return ent;
}

entt::entity makeSpirit(ServerGame& game) {
  auto ent = game.registry.create();
  game.registry.emplace<shared::MazeSpiritGrid>(ent, int8_t(0), int8_t(0));
  game.registry.emplace<shared::MazeTag>(ent);
  game.registry.emplace<shared::PhysicsBody>(ent, 0u);
  return ent;
}

}  // namespace

TEST(MazeSpiritControl, UpArrowDrivesGreenPieceNorth) {
  ServerGame game;
  makeSpirit(game);
  auto player = makeMazePlayer(game, 1);
  game.registry.get<shared::PlayerInput>(player).keys = KEY_SPIRIT_UP;

  const auto drive = maze_spirit_control::collectSpiritDriveFromPlayers(game);
  EXPECT_EQ(drive.activePushCount, 1);
  EXPECT_NEAR(drive.dy, 1.0f, 0.001f);
  EXPECT_NEAR(drive.dx, 0.0f, 0.001f);
}

TEST(MazeSpiritControl, DownArrowDrivesGreenPieceSouth) {
  ServerGame game;
  makeSpirit(game);
  auto player = makeMazePlayer(game, 2);
  game.registry.get<shared::PlayerInput>(player).keys = KEY_SPIRIT_DOWN;

  const auto drive = maze_spirit_control::collectSpiritDriveFromPlayers(game);
  EXPECT_EQ(drive.activePushCount, 1);
  EXPECT_NEAR(drive.dy, -1.0f, 0.001f);
}

TEST(MazeSpiritControl, NoArrowDoesNotPush) {
  ServerGame game;
  makeSpirit(game);
  makeMazePlayer(game, 2);

  const auto drive = maze_spirit_control::collectSpiritDriveFromPlayers(game);
  EXPECT_EQ(drive.activePushCount, 0);
  EXPECT_NEAR(drive.dx, 0.0f, 0.001f);
  EXPECT_NEAR(drive.dy, 0.0f, 0.001f);
}

TEST(MazeSpiritControl, OppositeArrowsCancel) {
  ServerGame game;
  makeSpirit(game);
  auto pUp = makeMazePlayer(game, 1);
  auto pDown = makeMazePlayer(game, 2);
  game.registry.get<shared::PlayerInput>(pUp).keys = KEY_SPIRIT_UP;
  game.registry.get<shared::PlayerInput>(pDown).keys = KEY_SPIRIT_DOWN;

  const auto drive = maze_spirit_control::collectSpiritDriveFromPlayers(game);
  EXPECT_EQ(drive.activePushCount, 2);
  EXPECT_NEAR(drive.dx, 0.0f, 0.001f);
  EXPECT_NEAR(drive.dy, 0.0f, 0.001f);
}

TEST(MazeSpiritControl, FindSharedSpirit) {
  ServerGame game;
  const entt::entity spirit = makeSpirit(game);
  EXPECT_EQ(maze_spirit_control::findSharedSpirit(game), spirit);
}
