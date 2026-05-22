#include <gtest/gtest.h>

#include "server/game/maze.h"
#include "server/game/overworld.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/input.h"

TEST(MazeLogic, EnterMazePuzzleStartsWinterPuzzleAndUI) {
  ServerGame game;
  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::LOBBY, 0u, 1000u);

  EnterMazePuzzle(game);

  EXPECT_EQ(game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase,
            shared::RunPhase::INPROGRESS);
  ASSERT_TRUE(game.registry.all_of<shared::MazeUIState>(puzzleEnt));
  EXPECT_TRUE(game.registry.get<shared::MazeUIState>(puzzleEnt).open);
}

TEST(MazeLogic, StepMemorySpiritRejectsWallAndAppliesPenalty) {
  ServerGame game;
  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::INPROGRESS, 0u, 1000u);

  auto spirit = game.registry.create();
  game.registry.emplace<shared::MazeSpiritGrid>(
      spirit, static_cast<int8_t>(0), static_cast<int8_t>(0));
  // Cell (1,0) index 1 in a 3-wide grid is a wall
  const uint64_t walls = (1ull << 1);
  EXPECT_FALSE(StepMemorySpirit(game, puzzleId, spirit, 1, 0, 3, 3, walls, 50u));
  EXPECT_EQ(game.registry.get<shared::MazeSpiritGrid>(spirit).gx, 0);
  EXPECT_EQ(game.registry.get<shared::PuzzleComponent>(puzzleEnt).puzzleElapsedTimeMs,
            50u);

  EXPECT_TRUE(StepMemorySpirit(game, puzzleId, spirit, 0, 1, 3, 3, walls, 0));
  EXPECT_EQ(game.registry.get<shared::MazeSpiritGrid>(spirit).gy, 1);
}

TEST(MazeLogic, ClaimDirectionPadOnlyWhileInProgress) {
  ServerGame game;
  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::LOBBY, 0u, 0u);

  auto p0 = game.registry.create();
  auto p1 = game.registry.create();
  auto p2 = game.registry.create();
  auto p3 = game.registry.create();

  ClaimDirectionPad(game, puzzleId, p0, p1, p2, p3);
  EXPECT_FALSE(game.registry.all_of<shared::MazePadBinding>(p0));

  game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase =
      shared::RunPhase::INPROGRESS;
  ClaimDirectionPad(game, puzzleId, p0, p1, p2, p3);
  EXPECT_EQ(game.registry.get<shared::MazePadBinding>(p0).pad,
            shared::MazeDirection::UP);
  EXPECT_EQ(game.registry.get<shared::MazePadBinding>(p1).pad,
            shared::MazeDirection::DOWN);
  EXPECT_EQ(game.registry.get<shared::MazePadBinding>(p2).pad,
            shared::MazeDirection::LEFT);
  EXPECT_EQ(game.registry.get<shared::MazePadBinding>(p3).pad,
            shared::MazeDirection::RIGHT);
}

TEST(MazeLogic, StepMemorySpiritGridMovesByDelta) {
  ServerGame game;
  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::INPROGRESS, 0u, 0u);

  auto spirit = game.registry.create();
  game.registry.emplace<shared::MazeSpiritGrid>(
      spirit, static_cast<int8_t>(4), static_cast<int8_t>(4));
  game.registry.emplace<shared::MazeTag>(spirit);

  ASSERT_TRUE(StepMemorySpirit(game, puzzleId, spirit, 1, 0, 8, 8, 0u, 0));
  EXPECT_EQ(game.registry.get<shared::MazeSpiritGrid>(spirit).gx, 5);
  ASSERT_TRUE(StepMemorySpirit(game, puzzleId, spirit, 0, 1, 8, 8, 0u, 0));
  EXPECT_EQ(game.registry.get<shared::MazeSpiritGrid>(spirit).gy, 5);
}

TEST(MazeLogic, CollectMazeFragmentFinishesWinter) {
  ServerGame game;
  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  auto [gsId, gsEnt] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::INPROGRESS, 0u, 0u);
  game.registry.emplace<shared::GameSection>(gsEnt);

  CollectMazeFragment(game);

  EXPECT_EQ(game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase,
            shared::RunPhase::FINISHED);
  EXPECT_TRUE(game.registry.get<shared::SectionController>(winterEnt).completed);
  EXPECT_EQ(game.registry.get<shared::GameSection>(gsEnt).sectionsCompleted, 1u);
  EXPECT_TRUE(RestoreWinterColor(game));
}

TEST(MazeLogic, ExitMazePuzzleClosesUIWhenFinished) {
  ServerGame game;
  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::FINISHED, 0u, 0u);
  game.registry.emplace<shared::MazeUIState>(puzzleEnt, true);

  ExitMazePuzzle(game);

  EXPECT_FALSE(game.registry.get<shared::MazeUIState>(puzzleEnt).open);
}

TEST(HubLogic, GatherAndOpenDoorAfterWinterDone) {
  ServerGame game;

  auto [puzzleId, puzzleEnt] = new_entity(game);
  auto [winterId, winterEnt] = new_entity(game);
  auto [fallId, fallEnt] = new_entity(game);
  auto [doorId, doorEnt] = new_entity(game);
  auto [swId, switchEnt] = new_entity(game);

  game.registry.emplace<shared::SectionController>(
      winterEnt, shared::SectionSeasonMap::WINTER, puzzleId, true, true);
  game.registry.emplace<shared::SectionController>(
      fallEnt, shared::SectionSeasonMap::FALL, 999u, false, false);
  game.registry.emplace<shared::PuzzleComponent>(
      puzzleEnt, shared::RunPhase::FINISHED, 0u, 0u);
  game.registry.emplace<shared::SectionDoorComponent>(doorEnt);
  game.registry.emplace<shared::SwitchComponent>(switchEnt, doorId, false);

  auto makePlayer = [&](float x, float y) {
    auto e = game.registry.create();
    game.registry.emplace<shared::Position>(e, x, y, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::OverworldTag>(e);
    return e;
  };
  makePlayer(0.0f, 0.0f);
  makePlayer(1.0f, 0.0f);
  makePlayer(0.0f, 1.0f);
  makePlayer(1.0f, 1.0f);

  GatherAtExitSwitch(game, switchEnt, -0.5f, -0.5f, 1.5f, 1.5f, 4u, shared::SectionSeasonMap::WINTER);
  EXPECT_TRUE(game.registry.get<shared::SwitchComponent>(switchEnt).switchOn);

  auto [gsId, gsEnt] = new_entity(game);
  game.registry.emplace<shared::GameSection>(
      gsEnt, shared::SectionSeasonMap::WINTER, static_cast<uint8_t>(1));

  OpenSectionDoor(game, doorEnt, switchEnt, fallEnt, shared::SectionSeasonMap::WINTER, shared::SectionSeasonMap::FALL);

  EXPECT_EQ(game.registry.get<shared::SectionDoorComponent>(doorEnt).state,
            shared::DoorState::OPEN);
  EXPECT_TRUE(game.registry.get<shared::SectionController>(fallEnt).unlocked);
  EXPECT_EQ(game.registry.get<shared::GameSection>(gsEnt).currentActiveSeason,
            shared::SectionSeasonMap::FALL);
}
