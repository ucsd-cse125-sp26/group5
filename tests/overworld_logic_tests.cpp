#include <gtest/gtest.h>

#include "server/game/overworld.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/input.h"

TEST(OverworldLogic, MoveInMainMapSkipsPlayersWithoutPhysicsBody) {
  ServerGame game;
  auto player = game.registry.create();
  game.registry.emplace<shared::Position>(player, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f);
  game.registry.emplace<shared::Velocity>(player, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::PlayerInput>(
      player, KEY_FORWARD, static_cast<InputKeys>(0), static_cast<InputKeys>(0),
      0.0f, 0.0f);
  game.registry.emplace<shared::OverworldTag>(player);

  MoveInMainMap(game, 0.5f);

  const auto& pos = game.registry.get<shared::Position>(player);
  EXPECT_FLOAT_EQ(pos.y, 0.0f);
}

TEST(OverworldLogic, MoveInMainMapKeepsWinterSeasonWhenWinterNotCompleted) {
  ServerGame game;

  auto gameSection = game.registry.create();
  game.registry.emplace<shared::GameSection>(gameSection,
                                             shared::SectionSeasonMap::FALL,
                                             static_cast<uint8_t>(0));

  auto winter = game.registry.create();
  game.registry.emplace<shared::SectionController>(
      winter, shared::SectionSeasonMap::WINTER, 123u, true, false);

  MoveInMainMap(game, 0.016f);

  const auto& section = game.registry.get<shared::GameSection>(gameSection);
  EXPECT_EQ(section.currentActiveSeason, shared::SectionSeasonMap::WINTER);
}

TEST(OverworldLogic, MoveInMainMapDoesNotMutateRunOrPuzzlePhase) {
  ServerGame game;

  auto run = game.registry.create();
  game.registry.emplace<shared::RunState>(run, shared::RunPhase::INPROGRESS,
                                          shared::Outcome::UNDECIDED);

  auto puzzle = game.registry.create();
  game.registry.emplace<shared::PuzzleComponent>(
      puzzle, shared::RunPhase::INPROGRESS, 100u, 1000u);

  MoveInMainMap(game, 0.016f);

  const auto& runState = game.registry.get<shared::RunState>(run);
  const auto& puzzleState = game.registry.get<shared::PuzzleComponent>(puzzle);
  EXPECT_EQ(runState.phase, shared::RunPhase::INPROGRESS);
  EXPECT_EQ(puzzleState.phase, shared::RunPhase::INPROGRESS);
}
