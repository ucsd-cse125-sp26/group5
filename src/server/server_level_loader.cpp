#include "server_level_loader.h"

#include "server_game.h"
#include "shared/components.h"

void loadLevel(ServerGame& game) {
  // Game attributes entity
  auto [gameControllerID, gameController] = new_entity(game);
  game.registry.emplace<shared::RunState>(gameController);
  game.registry.emplace<shared::GameSection>(gameController);
  game.registry.emplace<shared::TimeComponent>(gameController);

  // Entity holding Puzzle attributes
  auto [puzzleMazeID, puzzleMaze] = new_entity(game);
  game.registry.emplace<shared::PuzzleComponent>(puzzleMaze);

  auto [puzzleTypingID, puzzleTyping] = new_entity(game);
  game.registry.emplace<shared::PuzzleComponent>(puzzleTyping);

  auto [puzzleDecryptID, puzzleDecrypt] = new_entity(game);
  game.registry.emplace<shared::PuzzleComponent>(puzzleDecrypt);

  auto [puzzleTengramID, puzzleTengram] = new_entity(game);
  game.registry.emplace<shared::PuzzleComponent>(puzzleTengram);

  // entity holding game progress for each season
  auto [winterSectionID, winterSection] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterSection, shared::SectionSeasonMap::WINTER, puzzleMazeID,
      true,  // winter is first to unlock
      false  // not completed yet
  );
  auto [fallSectionID, fallSection] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      fallSection, shared::SectionSeasonMap::FALL, puzzleTypingID,
      false,  // fall is second to unlock
      false   // not completed yet
  );
  auto [summerSectionID, summerSection] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      summerSection, shared::SectionSeasonMap::SUMMER, puzzleDecryptID,
      false,  // summer is 3rd to unlock
      false   // not completed yet
  );
  auto [springSectionID, springSection] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      springSection, shared::SectionSeasonMap::SPRING, puzzleTengramID,
      false,  // spring is 4th to unlock
      false   // not completed yet
  );

  // door entities for each season
  auto [winterDoorID, winterDoor] = new_entity(game);
  // Temporary position values
  game.registry.emplace<shared::Position>(winterDoor, 100.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      winterDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      winterSectionID, 0.0f, -10.0f);
  game.registry.emplace<shared::OverworldTag>(winterDoor);

  auto [fallDoorID, fallDoor] = new_entity(game);
  // Temporary position values
  game.registry.emplace<shared::Position>(fallDoor, 110.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      fallDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      fallSectionID, 0.0f, -10.0f);
  game.registry.emplace<shared::OverworldTag>(fallDoor);

  auto [summerDoorID, summerDoor] = new_entity(game);
  // Temporary position values
  game.registry.emplace<shared::Position>(summerDoor, 120.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      summerDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      summerSectionID, 0.0f, -10.0f);
  game.registry.emplace<shared::OverworldTag>(summerDoor);

  auto [winterFragmentID, winterFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(winterFragment, 130.0f, 100.0f,
                                          100.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(winterFragment, "cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      winterFragment, shared::SectionSeasonMap::WINTER, false);
  game.registry.emplace<shared::OverworldTag>(winterFragment);

  auto [fallFragmentID, fallFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(fallFragment, 140.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(fallFragment, "cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      fallFragment, shared::SectionSeasonMap::FALL, false);
  game.registry.emplace<shared::OverworldTag>(fallFragment);

  auto [summerFragmentID, summerFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(summerFragment, 150.0f, 100.0f,
                                          100.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(summerFragment, "cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      summerFragment, shared::SectionSeasonMap::SUMMER, false);
  game.registry.emplace<shared::OverworldTag>(summerFragment);

  auto [springFragmentID, springFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(springFragment, 160.0f, 100.0f,
                                          100.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(springFragment, "cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      springFragment, shared::SectionSeasonMap::SPRING, false);
  game.registry.emplace<shared::OverworldTag>(springFragment);
}
