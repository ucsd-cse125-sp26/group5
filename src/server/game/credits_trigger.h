#pragma once

struct ServerGame;

namespace credits_trigger {

// True when the Fallen house region is loaded, at least one player is
// connected, and every active player's overworld avatar is inside that region.
bool allActivePlayersInFallenHouse(const ServerGame& game);

// Per-tick end-game check. On the rising edge of "all players inside" it
// broadcasts a STATE_CHANGE(CREDITS) once; re-arms when players leave.
void checkCreditsTrigger(ServerGame& game);

}  // namespace credits_trigger
