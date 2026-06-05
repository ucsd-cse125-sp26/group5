#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace maze_puzzle {

void initOverworldMazePuzzleController(ServerGame& game);

[[nodiscard]] bool isPuzzleActive(const ServerGame& game);

void beginPuzzle(ServerGame& game);
void endPuzzle(ServerGame& game);

void updatePuzzle(ServerGame& game, float dt);

// Keep overworld avatars inside the maze trigger pad (XY) while puzzle runs.
void clampPlayersToMazeTrigger(ServerGame& game);

// Clamp piece to preview board (XZ plane); call after physics sync.
void clampPieceToBoard(ServerGame& game);

// Call once per tick after physics sync if clamp set reachGoalPending.
void tryCompleteOnGoal(ServerGame& game);

}  // namespace maze_puzzle
