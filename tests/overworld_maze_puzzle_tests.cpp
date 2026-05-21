#include <gtest/gtest.h>

#include "server/game/maze_generation.h"
#include "server/game/maze_spirit_control.h"
#include "server/game/overworld_maze_puzzle.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/input.h"

namespace {

entt::entity makeOverworldPlayer(ServerGame& game, uint8_t slot) {
  auto ent = game.registry.create();
  game.registry.emplace<shared::Entity>(ent, game.nextEntityId++);
  game.registry.emplace<shared::OverworldTag>(ent);
  game.registry.emplace<shared::PlayerInput>(ent, InputKeys(0), InputKeys(0),
                                             InputKeys(0), 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(ent, "cube", 1.0f, 1.0f, 1.0f);
  game.registry.get<shared::RenderInfo>(ent).playerSlot = slot;
  return ent;
}

}  // namespace

TEST(OverworldMazePuzzle, EachPlayerOnlyPushesAssignedPad) {
  ServerGame game;
  auto pUp = makeOverworldPlayer(game, 1);
  auto pRight = makeOverworldPlayer(game, 4);
  game.registry.emplace<shared::MazePadBinding>(pUp, shared::MazeDirection::UP);
  game.registry.emplace<shared::MazePadBinding>(pRight,
                                                shared::MazeDirection::RIGHT);
  game.registry.get<shared::PlayerInput>(pUp).keys = KEY_SPIRIT_UP;
  game.registry.get<shared::PlayerInput>(pRight).keys = KEY_SPIRIT_RIGHT;

  const auto drive =
      maze_spirit_control::collectSpiritDriveFromOverworldPlayers(game);
  EXPECT_EQ(drive.activePushCount, 2);
  EXPECT_NEAR(drive.dy, 1.0f, 0.001f);
  EXPECT_NEAR(drive.dx, 1.0f, 0.001f);
}

TEST(OverworldMazePuzzle, WrongArrowForPadIsIgnored) {
  ServerGame game;
  auto player = makeOverworldPlayer(game, 1);
  game.registry.emplace<shared::MazePadBinding>(player, shared::MazeDirection::UP);
  game.registry.get<shared::PlayerInput>(player).keys = KEY_SPIRIT_RIGHT;

  const auto drive =
      maze_spirit_control::collectSpiritDriveFromOverworldPlayers(game);
  EXPECT_EQ(drive.activePushCount, 0);
}

TEST(OverworldMazePuzzle, GeneratedLayoutHasWalkableAndBlockedTiles) {
  constexpr int kW = 8;
  constexpr int kH = 8;
  constexpr uint32_t kSeed = 12505;
  const maze::MazeLayout layout =
      maze::GenerateMazeLayout(kW, kH, kSeed);
  const maze::MazeTileGrid grid = maze::ConvertToTileGrid(layout);

  bool hasWall = false;
  bool hasFloor = false;
  for (maze::MazeTile t : grid.tiles) {
    if (t == maze::MazeTile::Wall) hasWall = true;
    if (t == maze::MazeTile::Floor) hasFloor = true;
  }
  EXPECT_TRUE(hasWall);
  EXPECT_TRUE(hasFloor);
}

TEST(OverworldMazePuzzle, GoalTileMatchesGeneratedLayout) {
  constexpr int kW = 8;
  constexpr int kH = 8;
  constexpr uint32_t kSeed = 12505;
  const maze::MazeLayout layout =
      maze::GenerateMazeLayout(kW, kH, kSeed);
  const int goalTileX = layout.goalX * 2 + 1;
  const int goalTileY = layout.goalY * 2 + 1;
  const maze::MazeTileGrid grid = maze::ConvertToTileGrid(layout);
  EXPECT_EQ(grid.Tile(goalTileX, goalTileY), maze::MazeTile::Floor);
}

TEST(OverworldMazePuzzle, PuzzleInactiveByDefault) {
  ServerGame game;
  overworld_maze_puzzle::initOverworldMazePuzzleController(game);
  EXPECT_FALSE(overworld_maze_puzzle::isPuzzleActive(game));
  ASSERT_TRUE(game.registry.valid(game.overworldMazePuzzleController));
  EXPECT_FALSE(game.registry
                   .get<shared::OverworldMazePuzzleState>(
                       game.overworldMazePuzzleController)
                   .active);
}
