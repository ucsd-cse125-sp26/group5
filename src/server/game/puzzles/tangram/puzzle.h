#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace tangram_puzzle {

void initController(ServerGame& game);
void beginPuzzle(ServerGame& game);
void endPuzzle(ServerGame& game, bool releasePlayers = true);
void clampPlayersToPlayArena(ServerGame& game);

void updatePuzzle(ServerGame& game, float dt);
[[nodiscard]] bool isPuzzleActive(const ServerGame& game);

// Demo control: change the role-isolation stage of a live puzzle (0 = everyone
// can do everything; see shared::tangram_roles). Updates the synced state and
// re-syncs piece collision roles. No-op if the puzzle isn't active.
void setIsolationStage(ServerGame& game, uint8_t stage);

// Demo control: grant/revoke one ability for one player slot on top of the
// stage rules. ability: 0=push 1=rotate 2=color 3=slots. Re-syncs push
// collision layers when the push grant changes. No-op if not active.
void setPlayerGrant(ServerGame& game, uint8_t slot, uint8_t ability,
                    bool enable);
void clampPieceToArena(ServerGame& game, entt::entity ent);

// True when piece is at its slot position with correct rotation (win
// condition).
[[nodiscard]] bool isPieceCorrectlyPlaced(const ServerGame& game,
                                          entt::entity ent);

}  // namespace tangram_puzzle
