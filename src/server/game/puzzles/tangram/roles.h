#pragma once

#include <cstdint>
#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace tangram_role_server {

[[nodiscard]] uint8_t playerSlotForAvatar(const ServerGame& game,
                                          entt::entity avatar);

void applyCollisionRoles(ServerGame& game, uint8_t isolationStage);
void revertCollisionRoles(ServerGame& game);
// Apply push isolation at stage 5+; otherwise reset everyone to default push.
void syncCollisionRoles(ServerGame& game, uint8_t isolationStage);

// Per-piece layer from snap state and isolation stage.
void syncPieceCollisionLayer(ServerGame& game, entt::entity pieceEnt,
                             uint8_t isolationStage);

}  // namespace tangram_role_server
