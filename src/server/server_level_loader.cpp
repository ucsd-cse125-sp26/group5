#include "server_level_loader.h"

#include "server_game.h"
#include "shared/components.h"

void loadLevel(ServerGame& game) {
  // Game attributes entity
  auto [gameControllerID, gameController] = new_entity(game);
  game.registry.emplace<shared::RunState>(gameController);
  game.registry.emplace<shared::GameSection>(gameController);
  game.registry.emplace<shared::TimeComponent>(gameController);

  // Puzzle attributes entity
  auto [puzzleID, puzzle1] = new_entity(game);
  game.registry.emplace<shared::PuzzleComponent>(puzzle1);

  // Shared memory spirit for maze grid (logic only; minimal rendering if synced later).
  auto [spiritNumericId, spiritEnt] = new_entity(game);
  (void)spiritNumericId;
  game.registry.emplace<shared::MazeSpiritGrid>(
      spiritEnt, static_cast<int8_t>(0), static_cast<int8_t>(0));
  game.registry.emplace<shared::Position>(spiritEnt, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f);
  game.registry.emplace<shared::Velocity>(spiritEnt, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(spiritEnt, "cube", 0.8f);
  game.registry.emplace<shared::MazeTag>(spiritEnt);
  JPH::BodyID spiritBody = game.physics.createPlayerBody(0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::PhysicsBody>(
      spiritEnt, spiritBody.GetIndexAndSequenceNumber());

  // Visual maze board: 8x8 checker tiles to make shared-cube movement easier to read.
  constexpr int kGrid = 8;
  constexpr float kCell = 2.0f;
  constexpr float kTileScale = 0.45f;
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      auto [tileId, tile] = new_entity(game);
      (void)tileId;
      game.registry.emplace<shared::Position>(
          tile, static_cast<float>(gx) * kCell, static_cast<float>(gy) * kCell, -0.8f,
          1.0f, 0.0f, 0.0f, 0.0f);
      game.registry.emplace<shared::RenderInfo>(tile, "cube", kTileScale);
      game.registry.emplace<shared::MazeTag>(tile);
    }
  }

  auto [winterSectionID, winterSection] = new_entity(game);
  game.registry.emplace<shared::SectionController>(
      winterSection, shared::SectionSeasonMap::WINTER, puzzleID,
      true,  // winter is first to unlock
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
}
