#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace tangram_puzzle {

void initController(ServerGame& game);
void beginPuzzle(ServerGame& game);
void endPuzzle(ServerGame& game);
void clampPlayersToPlayArena(ServerGame& game);

void updatePuzzle(ServerGame& game, float dt);
[[nodiscard]] bool isPuzzleActive(const ServerGame& game);
void clampPieceToArena(ServerGame& game, entt::entity ent);

// True when piece is at its slot position with correct rotation (win
// condition).
[[nodiscard]] bool isPieceCorrectlyPlaced(const ServerGame& game,
                                          entt::entity ent);

}  // namespace tangram_puzzle
