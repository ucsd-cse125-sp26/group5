#include "server_level_loader.h"

#include "server_game.h"
#include "shared/components.h"

// loads in puzzles, doors, sections
void loadLevel(ServerGame& game) {
  // Entity holding game progress
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
		false  // not completed yet
  );
	auto [summerSectionID, summerSection] = new_entity(game);
	game.registry.emplace<shared::SectionController>(
		summerSection, shared::SectionSeasonMap::SUMMER, puzzleDecryptID,
		false,  // summer is 3rd to unlock
		false  // not completed yet
  );
	auto [springSectionID, SpringSection] = new_entity(game);
	game.registry.emplace<shared::SectionController>(
		summerSection, shared::SectionSeasonMap::SPRING, puzzleTengramID,
		false,  // spring is 4th to unlock
		false  // not completed yet
  );
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
  game.registry.emplace<shared::Position>(fallDoor, 100.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      fallDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      fallSectionID, 0.0f, -10.0f);
	game.registry.emplace<shared::OverworldTag>(fallDoor);

	auto [summerDoorID, summerDoor] = new_entity(game);
  // Temporary position values
  game.registry.emplace<shared::Position>(summerDoor, 100.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      summerDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      summerSectionID, 0.0f, -10.0f);
	game.registry.emplace<shared::OverworldTag>(summerDoor);
}
