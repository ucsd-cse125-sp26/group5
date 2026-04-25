#include "server_level_loader.h"

#include "server_game.h"
#include "shared/components.h"

void loadLevel(ServerGame& game, entt::registry& registry) {
  // Game attributes entity
  auto gameController = registry.create();
  registry.emplace<shared::RunState>(gameController);
  registry.emplace<shared::GameSection>(gameController);
  registry.emplace<shared::TimeComponent>(gameController);

  // Puzzle attributes entity
  auto [puzzleID, puzzle1] = new_entity(game);
  registry.emplace<shared::PuzzleComponent>(puzzle1);

  auto [winterSectionID, winterSection] = new_entity(game);
  registry.emplace<shared::SectionController>(
      winterSection, shared::SectionSeasonMap::WINTER, puzzleID,
      true,  // winter is first to unlock
      false  // not completed yet
  );

  auto sectionDoor = registry.create();
  // Temporary position values
  registry.emplace<shared::Position>(sectionDoor, 100.0f, 100.0f, 100.0f, 1.0f,
                                     0.0f, 0.0f, 0.0f);
  registry.emplace<shared::SectionDoorComponent>(
      sectionDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      winterSectionID);
}
