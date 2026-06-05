#include "server/game/decrypt_puzzle.h"

#include <cstdio>
#include <cstring>

#include <enet/enet.h>

#include "server/game/credits_trigger.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/puzzles/decrypt/defaults.h"
#include "shared/sound_constants.h"

namespace decrypt_puzzle {
namespace {

entt::entity controllerEntity(const ServerGame& game) {
  if (game.registry.valid(game.decryptController) &&
      game.registry.all_of<shared::DecryptPuzzleState>(game.decryptController)) {
    return game.decryptController;
  }
  auto view = game.registry.view<shared::DecryptPuzzleState>();
  for (auto e : view) return e;
  return entt::null;
}

shared::DecryptPuzzleState* state(ServerGame& game) {
  const entt::entity e = controllerEntity(game);
  if (!game.registry.valid(e)) return nullptr;
  return &game.registry.get<shared::DecryptPuzzleState>(e);
}

void playSound(ServerGame& game, shared::SoundId id) {
  if (game.network == nullptr) return;
  shared::SoundEventPacket pkt;
  pkt.soundId = static_cast<uint32_t>(id);
  pkt.volume = 1.0f;
  pkt.positional = false;
  net::broadcastPacket(game.network->getHost(), pkt);
}

void broadcastStateChange(ServerGame& game, shared::GameStateType stateType) {
  if (game.network == nullptr) return;
  shared::StateChangePacket pkt;
  pkt.state = stateType;
  net::broadcastPacket(game.network->getHost(), pkt);
}

void markActive(ServerGame& game) {
  shared::DecryptPuzzleState* st = state(game);
  if (st == nullptr || st->active || st->solved) return;
  st->active = true;
  broadcastStateChange(game, shared::GameStateType::DECRYPT);
  printf("[Decrypt] All players in the Fallen house — puzzle active.\n");
}

void markSolved(ServerGame& game) {
  shared::DecryptPuzzleState* st = state(game);
  if (st == nullptr || st->solved) return;
  st->solved = true;
  st->active = false;
  playSound(game, shared::SoundId::PUZZLE_SOLVED);
  printf("[Decrypt] Correct answer — rolling credits.\n");
  credits_trigger::checkCreditsTrigger(game);
}

}  // namespace

bool isSolved(const ServerGame& game) {
  const entt::entity e = controllerEntity(game);
  if (!game.registry.valid(e)) return false;
  return game.registry.get<shared::DecryptPuzzleState>(e).solved;
}

void update(ServerGame& game) {
  if (isSolved(game)) return;
  if (!credits_trigger::allActivePlayersInFallenHouse(game)) return;
  markActive(game);
}

void handleSubmitFromPeer(ServerGame& game, void* senderPeer, const char* text) {
  auto* sender = static_cast<ENetPeer*>(senderPeer);
  if (text == nullptr || isSolved(game)) return;
  shared::DecryptPuzzleState* st = state(game);
  if (st == nullptr || !st->active) return;

  if (shared::decrypt::answersMatch(text)) {
    markSolved(game);
    return;
  }

  playSound(game, shared::SoundId::PUZZLE_FAILED);
  if (game.network != nullptr && sender != nullptr) {
    shared::DecryptResultPacket pkt;
    pkt.accepted = 0;
    net::sendPacket(sender, pkt);
  }
  printf("[Decrypt] Incorrect answer submitted.\n");
}

void debugActivate(ServerGame& game) {
  shared::DecryptPuzzleState* st = state(game);
  if (st == nullptr || st->solved) return;
  markActive(game);
}

}  // namespace decrypt_puzzle
