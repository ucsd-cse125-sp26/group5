#pragma once

struct ServerGame;

namespace decrypt_puzzle {

void update(ServerGame& game);
void handleSubmitFromPeer(ServerGame& game, void* senderPeer, const char* text);
void debugActivate(ServerGame& game);

[[nodiscard]] bool isSolved(const ServerGame& game);

}  // namespace decrypt_puzzle
