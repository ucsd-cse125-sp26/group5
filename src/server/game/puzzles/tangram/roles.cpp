#include "server/game/puzzles/tangram/roles.h"

#include <cstdio>

#include "server/game/puzzles/tangram/puzzle.h"
#include "server/physics_engine.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/puzzles/tangram/roles.h"

namespace tangram_role_server {

uint8_t playerSlotForAvatar(const ServerGame& game, entt::entity avatar) {
  if (!game.registry.valid(avatar) ||
      !game.registry.all_of<shared::RenderInfo>(avatar)) {
    return 0;
  }
  const uint8_t slot = game.registry.get<shared::RenderInfo>(avatar).playerSlot;
  if (slot < 1 || slot > 4) return 0;
  return slot;
}

namespace {

void setBodyLayer(ServerGame& game, entt::entity ent, JPH::ObjectLayer layer) {
  if (!game.registry.all_of<shared::PhysicsBody>(ent)) return;
  auto& bodyInterface = game.physics.getBodyInterface();
  auto& pb = game.registry.get<shared::PhysicsBody>(ent);
  JPH::BodyID body(pb.bodyId);
  if (!bodyInterface.IsAdded(body)) return;
  bodyInterface.SetObjectLayer(body, layer);
  bodyInterface.ActivateBody(body);
}

JPH::ObjectLayer pieceLayerFor(ServerGame& game, entt::entity ent,
                               uint8_t isolationStage) {
  if (game.registry.all_of<shared::TangramPiece>(ent)) {
    const auto& piece = game.registry.get<shared::TangramPiece>(ent);
    if (piece.slotSnapped ||
        tangram_puzzle::isPieceCorrectlyPlaced(game, ent)) {
      return Layers::TANGRAM_SNAPPED;
    }
  }
  if (!shared::tangram_roles::pushCollisionIsolation(isolationStage)) {
    return Layers::MOVING;
  }
  return Layers::TANGRAM;
}

}  // namespace

void syncPieceCollisionLayer(ServerGame& game, entt::entity pieceEnt,
                             uint8_t isolationStage) {
  if (!game.registry.all_of<shared::OverworldTangramPiece, shared::PhysicsBody>(
          pieceEnt)) {
    return;
  }
  setBodyLayer(game, pieceEnt, pieceLayerFor(game, pieceEnt, isolationStage));
}

void applyCollisionRoles(ServerGame& game, uint8_t isolationStage) {
  if (!shared::tangram_roles::pushCollisionIsolation(isolationStage)) return;

  uint8_t grantPush = 0;
  if (game.registry.valid(game.overworldTangramController) &&
      game.registry.all_of<shared::OverworldTangramPuzzleState>(
          game.overworldTangramController)) {
    grantPush = game.registry
                    .get<shared::OverworldTangramPuzzleState>(
                        game.overworldTangramController)
                    .grantPush;
  }

  auto playerView =
      game.registry.view<shared::OverworldTag, shared::PhysicsBody,
                         shared::PlayerInput, shared::RenderInfo>();
  for (auto ent : playerView) {
    const uint8_t slot = playerSlotForAvatar(game, ent);
    const JPH::ObjectLayer layer =
        shared::tangram_roles::canPush(isolationStage, slot, grantPush)
            ? Layers::MOVING
            : Layers::MOVING_NO_TANGRAM;
    setBodyLayer(game, ent, layer);
  }

  auto pieceView =
      game.registry.view<shared::OverworldTangramPiece, shared::PhysicsBody>();
  for (auto ent : pieceView) {
    syncPieceCollisionLayer(game, ent, isolationStage);
  }
}

void revertCollisionRoles(ServerGame& game) {
  auto playerView =
      game.registry.view<shared::OverworldTag, shared::PhysicsBody,
                         shared::PlayerInput>();
  for (auto ent : playerView) {
    setBodyLayer(game, ent, Layers::MOVING);
  }

  auto pieceView =
      game.registry.view<shared::OverworldTangramPiece, shared::PhysicsBody>();
  for (auto ent : pieceView) {
    setBodyLayer(game, ent, Layers::MOVING);
  }
}

void syncCollisionRoles(ServerGame& game, uint8_t isolationStage) {
  if (shared::tangram_roles::pushCollisionIsolation(isolationStage)) {
    applyCollisionRoles(game, isolationStage);
  } else {
    revertCollisionRoles(game);
  }
}

}  // namespace tangram_role_server
