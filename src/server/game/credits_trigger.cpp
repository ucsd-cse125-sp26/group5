#include "server/game/credits_trigger.h"

#include <cstdio>

#include "server/game/decrypt_puzzle.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"

namespace credits_trigger {

bool allActivePlayersInFallenHouse(const ServerGame& game) {
  if (!game.fallenHouseRegion.valid || game.active_players.empty()) {
    return false;
  }
  for (const auto& [peer, slots] : game.active_players) {
    (void)peer;
    if (!game.registry.valid(slots.overworld_avatar) ||
        !game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      return false;
    }
    const auto& pos =
        game.registry.get<shared::Position>(slots.overworld_avatar);
    if (!game.fallenHouseRegion.contains(pos.x, pos.y, pos.z)) return false;
  }
  return true;
}

void checkCreditsTrigger(ServerGame& game) {
  // Credits roll exactly once per server lifetime, after decryption succeeds.
  if (game.creditsRolled) return;
  if (!decrypt_puzzle::isSolved(game)) return;
  if (!allActivePlayersInFallenHouse(game)) return;
  game.creditsRolled = true;

  shared::StateChangePacket pkt;
  pkt.state = shared::GameStateType::CREDITS;
  net::broadcastPacket(game.network->getHost(), pkt);
  printf("[Credits] Decryption solved — rolling credits.\n");
}

}  // namespace credits_trigger
