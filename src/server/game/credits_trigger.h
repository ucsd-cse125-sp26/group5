#pragma once

struct ServerGame;

namespace credits_trigger {

// True when the Fallen house region is loaded, at least one player is
// connected, and every active player's overworld avatar is inside that region.
bool allActivePlayersInFallenHouse(const ServerGame& game);

// Per-tick end-game check. After decryption is solved and all players are
// inside the Fallen house, broadcasts STATE_CHANGE(CREDITS) once.
void checkCreditsTrigger(ServerGame& game);

}  // namespace credits_trigger
