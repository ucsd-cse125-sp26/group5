#include "server_level_loader.h"

#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float3.hpp"
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
  // Shared memory spirit: visible green piece players move through the maze
  // toward the orange goal marker.
  auto [spiritNumericId, spiritEnt] = new_entity(game);
  (void)spiritNumericId;
  game.registry.emplace<shared::MazeSpiritGrid>(
      spiritEnt, static_cast<int8_t>(0), static_cast<int8_t>(0));
  game.registry.emplace<shared::Position>(spiritEnt, 0.0f, 0.0f, 0.0f, 1.0f,
                                          0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::Velocity>(spiritEnt, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(spiritEnt, "start_cube", 0.8f, 0.8f,
                                            0.8f);
  game.registry.emplace<shared::MazeTag>(spiritEnt);
  JPH::BodyID spiritBody = game.physics.createPlayerBody(
      "start_cube", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
      glm::vec3(0.8f));
  game.registry.emplace<shared::PhysicsBody>(
      spiritEnt, spiritBody.GetIndexAndSequenceNumber());

  // Visual maze board: 8x8 checker tiles to make shared-cube movement easier
  // to read.
  constexpr int kGrid = 8;
  constexpr float kCell = 2.0f;
  constexpr float kTileScale = 0.45f;
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      auto [tileId, tile] = new_entity(game);
      (void)tileId;
      game.registry.emplace<shared::Position>(
          tile, static_cast<float>(gx) * kCell, static_cast<float>(gy) * kCell,
          -0.8f, 1.0f, 0.0f, 0.0f, 0.0f);
      game.registry.emplace<shared::RenderInfo>(tile, "cube", kTileScale,
                                                kTileScale, kTileScale);
      game.registry.emplace<shared::MazeTag>(tile);
    }
  }

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
  game.registry.emplace<shared::Position>(winterDoor, 100.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      winterDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      winterSectionID);      
  game.registry.emplace<shared::OverworldTag>(winterDoor);

  auto [fallDoorID, fallDoor] = new_entity(game);
  game.registry.emplace<shared::Position>(fallDoor, 110.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      fallDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      fallSectionID);          
  game.registry.emplace<shared::OverworldTag>(fallDoor);

  auto [summerDoorID, summerDoor] = new_entity(game);
  game.registry.emplace<shared::Position>(summerDoor, 120.0f, 100.0f, 100.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::SectionDoorComponent>(
      summerDoor, shared::DoorState::CLOSED,
      static_cast<uint8_t>(4),  // required players to open
      summerSectionID);         
  game.registry.emplace<shared::OverworldTag>(summerDoor);

  auto [winterFragmentID, winterFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(winterFragment, 65.0f, 80.0f, 0.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  // RenderInfo is intentionally omitted: the winter fragment is invisible until
  // CollectMazeFragment adds it when the maze puzzle is solved.
  game.registry.emplace<shared::FragmentComponent>(
      winterFragment, shared::SectionSeasonMap::WINTER, false);
  game.registry.emplace<shared::OverworldTag>(winterFragment);

  auto [fallFragmentID, fallFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(fallFragment, 130.0f, 65.0f, 22.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  // for testing, fragments will be light cubes
  game.registry.emplace<shared::RenderInfo>(fallFragment, "light_cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      fallFragment, shared::SectionSeasonMap::FALL, false);
  game.registry.emplace<shared::OverworldTag>(fallFragment);

  auto [summerFragmentID, summerFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(summerFragment, 105.0f, -40.0f, 45.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  // for testing, fragments will be light cubes
  game.registry.emplace<shared::RenderInfo>(summerFragment, "light_cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      summerFragment, shared::SectionSeasonMap::SUMMER, false);
  game.registry.emplace<shared::OverworldTag>(summerFragment);

  auto [springFragmentID, springFragment] = new_entity(game);
  game.registry.emplace<shared::Position>(springFragment, -65.0f, 0.0f, 70.0f,
                                          1.0f, 0.0f, 0.0f, 0.0f);
  // for testing, fragments will be light cubes
  game.registry.emplace<shared::RenderInfo>(springFragment, "light_cube", 0.5f);
  game.registry.emplace<shared::FragmentComponent>(
      springFragment, shared::SectionSeasonMap::SPRING, false);
  game.registry.emplace<shared::OverworldTag>(springFragment);
}
