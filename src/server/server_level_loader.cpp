#include "server_level_loader.h"

#include "server_game.h"
#include "shared/components.h"

void loadLevel(ServerGame& game) {
  // Entity holding game progress
  auto [gameControllerID, gameController] = new_entity(game);
  game.registry.emplace<shared::RunState>(gameController);
  game.registry.emplace<shared::GameSection>(gameController);
  game.registry.emplace<shared::TimeComponent>(gameController);




  // Entity holding Puzzle attributes
  auto [puzzleID, puzzle1] = new_entity(game);
  game.registry.emplace<shared::PuzzleComponent>(puzzle1);

	auto [winterSectionID, winterSection] = new_entity(game);
	game.registry.emplace<shared::SectionController>(
		winterSection, shared::SectionSeasonMap::WINTER, puzzleID,
		true,  // winter is first to unlock
		false  // not completed yet
  );
	auto [fallSectionID, fallSection] = new_entity(game);
	game.registry.emplace<shared::SectionController>(
		fallSection, shared::SectionSeasonMap::FALL, puzzleID,
		false,  // fall is second to unlock
		false  // not completed yet
  );
	auto [summerSectionID, summerSection] = new_entity(game);
	game.registry.emplace<shared::SectionController>(
		summerSection, shared::SectionSeasonMap::SUMMER, puzzleID,
		false,  // summer is 3rd to unlock
		false  // not completed yet
  );
	auto [springSectionID, SpringSection] = new_entity(game);
	game.registry.emplace<shared::SectionController>(
		summerSection, shared::SectionSeasonMap::SPRING, puzzleID,
		false,  // spring is 4th to unlock
		false  // not completed yet
  );
  auto [sectionDoorID, sectionDoor] = new_entity(game);
  // Temporary position values
  game.registry.emplace<shared::Position>(sectionDoor, 100.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      sectionDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      winterSectionID, 0.0f, -10.0f);
	game.registry.emplace<shared::OverworldTag>(sectionDoor);
}
