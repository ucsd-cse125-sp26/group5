#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <thread>

#include "game_state.h"
#include "server/game/overworld.h"
#include "server/game/puzzles/maze/camera.h"
#include "server/game/puzzles/maze/puzzle.h"
#include "server/game/puzzles/maze/trigger.h"
#include "server/game/puzzles/tangram/camera.h"
#include "server/game/puzzles/tangram/puzzle.h"
#include "server/game/puzzles/tangram/roles.h"
#include "server/game/puzzles/tangram/trigger.h"
#include "server_debug.h"
#include "server_game.h"
#include "server_level_loader.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/dev_spawn.h"
#include "shared/hello.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/simple_profiler.h"

namespace {

bool shouldSendFrameUpdate(entt::registry& registry, entt::entity ent) {
  // Static map / wall / marker entities are sent by SPAWN_ENTITY when a state
  // is entered or a client connects. Per-frame UPDATE_ENTITY should stay small:
  // only entities whose synced components can change every tick belong here.
  return registry.any_of<
      shared::PlayerInput, shared::Velocity, shared::PointLight,
      shared::DirectionalLight, shared::Scene, shared::FragmentComponent,
      shared::OverworldTangramPiece, shared::FallChallengeState,
      shared::SummerEscapeState, shared::FallingObject>(ent);
}

}  // namespace

int main() {
  std::cout << "Hello World Server";
  shared::hello();

  ServerGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  initServerGame(game);

  ServerNetwork network;
  if (!network.init(7777, 4)) {
    return EXIT_FAILURE;
  }
  game.network = &network;

  registerServerHandlers(network);

  loadLevel(game);
  initWorldEntities(game);

  if (shared::dev_spawn::kOverworldSpawn ==
      shared::dev_spawn::OverworldSpawn::Tangram) {
    printf("[DevSpawn] Overworld connect spawn: tangram pad\n");
  } else {
    printf("[DevSpawn] Overworld connect spawn: winter maze\n");
  }

  // Start in the Overworld
  game.gameStateManager.changeState(game, std::make_unique<OverworldState>());

  network.onConnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("A new client connected from %x:%u.\n", peer->address.host,
           peer->address.port);

    if (g.unused_player_slots.empty()) {
      enet_peer_disconnect(peer, 0);
      return;
    }

    peer->data = (void*)"Client information";
    PlayerAvatars slots = g.unused_player_slots.back();
    g.unused_player_slots.pop_back();

    // Reuse the lowest free display slot (1–4) among connected players so a
    // reconnect re-fills the vacated slot instead of climbing past 4. A slot>4
    // breaks tangram roles (push/rotate) and maze direction bindings.
    bool slotUsed[5] = {};
    for (const auto& [otherPeer, otherSlots] : g.active_players) {
      (void)otherPeer;
      const entt::entity av = otherSlots.overworld_avatar;
      if (g.registry.valid(av) && g.registry.all_of<shared::RenderInfo>(av)) {
        const uint8_t used = g.registry.get<shared::RenderInfo>(av).playerSlot;
        if (used >= 1 && used <= 4) slotUsed[used] = true;
      }
    }
    uint8_t slot = 1;
    for (uint8_t i = 1; i <= 4; ++i) {
      if (!slotUsed[i]) {
        slot = i;
        break;
      }
    }

    g.active_players[peer] = slots;
    for (entt::entity av : {slots.overworld_avatar, slots.maze_avatar}) {
      if (g.registry.valid(av) && g.registry.all_of<shared::RenderInfo>(av)) {
        g.registry.get<shared::RenderInfo>(av).playerSlot = slot;
      }
    }
    printf("[Server] Client assigned player slot %" PRIu8 "\n", slot);

    if (g.gameStateManager.currentState() &&
        g.gameStateManager.currentState()->getStateType() ==
            StateType::OVERWORLD) {
      if (g.registry.valid(slots.overworld_avatar) &&
          g.registry.all_of<shared::Position>(slots.overworld_avatar)) {
        // Keep the avatar at its spawned-in position (where this pool slot
        // was created and has settled) instead of teleporting every login to
        // the same join-slot spawn point.
        auto& pos = g.registry.get<shared::Position>(slots.overworld_avatar);
        if (shared::dev_spawn::kOverworldSpawn ==
            shared::dev_spawn::OverworldSpawn::Tangram) {
          tangram_camera::snapOverworldAvatarFaceTangramBoard(
              g, slots.overworld_avatar);
        } else {
          maze_camera::snapOverworldAvatarFaceMazePreview(
              g, slots.overworld_avatar);
        }
        tangram_role_server::revertCollisionRoles(g);
        if (g.registry.all_of<shared::PhysicsBody>(slots.overworld_avatar)) {
          auto& pb =
              g.registry.get<shared::PhysicsBody>(slots.overworld_avatar);
          auto& bi = g.physics.getBodyInterface();
          JPH::BodyID body(pb.bodyId);
          if (!bi.IsAdded(body)) {
            bi.AddBody(body, JPH::EActivation::Activate);
          }
          bi.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                         JPH::EActivation::Activate);
          bi.SetLinearVelocity(body, JPH::Vec3::sZero());
          bi.SetAngularVelocity(body, JPH::Vec3::sZero());
        }
        auto buf = serializeEntities(g.registry, g.componentRegistry,
                                     shared::PacketType::UPDATE_ENTITY,
                                     {slots.overworld_avatar}, false);
        net::sendRaw(peer, buf.data(), buf.size());
      }
    }

    auto* currentState = g.gameStateManager.currentState();
    entt::entity activeEntity = currentState->getClientAvatar(slots);
    std::vector<entt::entity> existing = currentState->getStateEntities(g);

    if (!existing.empty()) {
      auto buf =
          serializeEntities(g.registry, g.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, existing, false);
      net::sendRaw(peer, buf.data(), buf.size());
    }

    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId = g.registry.get<shared::Entity>(activeEntity).id;
    net::sendPacket(peer, assignPkt);
    shared::StateChangePacket statePkt;
    if (currentState->getStateType() == StateType::OVERWORLD) {
      statePkt.state = shared::GameStateType::OVERWORLD;
    } else {
      statePkt.state = shared::GameStateType::MAZE;
    }
    net::sendPacket(peer, statePkt);
    if (statePkt.state == shared::GameStateType::OVERWORLD) {
      syncOverworldSeasonMusic(g);
    }
  };

  network.onDisconnect = [&network](ServerGame& g, ENetPeer* peer) {
    auto it = g.active_players.find(peer);
    if (it == g.active_players.end()) return;

    printf("%s disconnected.\n", (const char*)peer->data);
    PlayerAvatars slots = it->second;

    // if we wanted to immediately despawn the player's avatar on disconnect, we
    // could do it here.

    // shared::DespawnPacket despawnPkt;
    // despawnPkt.type = shared::PacketType::DESPAWN_ENTITY;

    // // Both slots
    // auto despawnAvatar = [&](entt::entity e) {
    //   if (g.registry.valid(e)) {
    //     despawnPkt.entityId = g.registry.get<shared::Entity>(e).id;
    //     net::broadcastPacket(network.getHost(), despawnPkt);

    //     if (g.registry.all_of<shared::PhysicsBody>(e)) {
    //       auto& pb = g.registry.get<shared::PhysicsBody>(e);
    //       g.physics.destroyBody(pb.bodyId);
    //     }
    //     g.registry.destroy(e);
    //   }
    // };
    // despawnAvatar(slots.overworld_avatar);
    // despawnAvatar(slots.maze_avatar);

    for (entt::entity av : {slots.overworld_avatar, slots.maze_avatar}) {
      if (g.registry.valid(av) && g.registry.all_of<shared::RenderInfo>(av)) {
        g.registry.get<shared::RenderInfo>(av).playerSlot = 0;
      }
    }
    slots.resetControls(g.registry);
    if (g.registry.valid(slots.overworld_avatar) &&
        g.registry.all_of<shared::PhysicsBody>(slots.overworld_avatar)) {
      auto& pb = g.registry.get<shared::PhysicsBody>(slots.overworld_avatar);
      JPH::BodyID body(pb.bodyId);
      auto& bi = g.physics.getBodyInterface();
      if (bi.IsAdded(body)) {
        bi.RemoveBody(body);
      }
    }
    g.unused_player_slots.push_back(slots);
    g.active_players.erase(it);
    if (g.active_players.empty()) {
      g.nextPlayerJoinSlot = 1;
    }
    peer->data = nullptr;
  };

  auto previousTime = std::chrono::high_resolution_clock::now();
  const float fixedDt = 1.0f / 180.0f;
  float accumulator = 0.0f;

  while (true) {
    network.poll(game);

    // Run any debug-panel commands received this poll on the game thread,
    // before the fixed-step update consumes the resulting state.
    server_debug::processPendingCommands(game);

    auto currentTime = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(currentTime - previousTime).count();
    previousTime = currentTime;
    accumulator += dt;
    while (accumulator >= fixedDt) {
      game.physics.step(fixedDt);
      update_grounded_system(game);

      game.gameStateManager.update(game, fixedDt);

      // Jolt → ECS sync. Skip rotation for player entities; their yaw is
      // movement_system's responsibility (Jolt rotation DOFs are locked).
      auto& bi = game.physics.getBodyInterface();
      auto physicsView =
          game.registry.view<shared::Position, shared::PhysicsBody>();
      for (auto ent : physicsView) {
        auto& pos = physicsView.get<shared::Position>(ent);
        auto& pb = physicsView.get<shared::PhysicsBody>(ent);
        JPH::BodyID id(pb.bodyId);
        if (!bi.IsAdded(id)) continue;

        JPH::RVec3 jp = bi.GetPosition(id);
        pos.x = jp.GetX();
        pos.y = jp.GetY();
        pos.z = jp.GetZ();

        if (game.registry.all_of<shared::OverworldMazePiece>(ent)) {
          maze_puzzle::clampPieceToBoard(game);
        }
        if (game.registry.all_of<shared::OverworldTangramPiece>(ent)) {
          if (!game.registry.all_of<shared::PlayerInput>(ent)) {
            JPH::Quat jr = bi.GetRotation(id);
            pos.qw = jr.GetW();
            pos.qx = jr.GetX();
            pos.qy = jr.GetY();
            pos.qz = jr.GetZ();
          }
          tangram_puzzle::clampPieceToArena(game, ent);
          JPH::Vec3 v = bi.GetLinearVelocity(id);
          constexpr float kStopEps = 0.045f;
          if (v.LengthSq() < kStopEps * kStopEps) {
            bi.SetLinearVelocity(id, JPH::Vec3::sZero());
          }
        }
        if (game.registry.all_of<shared::MazeSpiritGrid>(ent)) {
          constexpr float kMazeTileSpacing = 1.5f;
          constexpr float kMin = 0.0f;
          constexpr float kMax = 21.0f;
          bool bounced = false;
          if (pos.x < kMin) {
            pos.x = kMin;
            bounced = true;
          } else if (pos.x > kMax) {
            pos.x = kMax;
            bounced = true;
          }
          if (pos.y < kMin) {
            pos.y = kMin;
            bounced = true;
          } else if (pos.y > kMax) {
            pos.y = kMax;
            bounced = true;
          }
          if (bounced) {
            bi.SetPosition(id, JPH::RVec3(pos.x, pos.y, pos.z),
                           JPH::EActivation::Activate);
            JPH::Vec3 v = bi.GetLinearVelocity(id);
            bi.SetLinearVelocity(id, JPH::Vec3(0.0f, 0.0f, v.GetZ()));
          }
          auto& grid = game.registry.get<shared::MazeSpiritGrid>(ent);
          const int gxCell =
              static_cast<int>(std::lround(pos.x / kMazeTileSpacing)) + 1;
          const int gyCell =
              static_cast<int>(std::lround(pos.y / kMazeTileSpacing)) + 1;
          grid.gx = static_cast<int8_t>(std::clamp(gxCell, 0, 16));
          grid.gy = static_cast<int8_t>(std::clamp(gyCell, 0, 16));
        }
        if (!game.registry.all_of<shared::PlayerInput>(ent) &&
            !game.registry.all_of<shared::OverworldTangramPiece>(ent)) {
          JPH::Quat jr = bi.GetRotation(id);
          pos.qw = jr.GetW();
          pos.qx = jr.GetX();
          pos.qy = jr.GetY();
          pos.qz = jr.GetZ();
        }
      }
      if (game.overworldTangramActive) {
        tangram_puzzle::clampPlayersToPlayArena(game);
      }
      if (maze_puzzle::shouldConfinePlayersToMazeTrigger(game)) {
        maze_puzzle::clampPlayersToMazeTrigger(game);
      }
      maze_puzzle::tryCompleteOnGoal(game);
      accumulator -= fixedDt;

      {
        // Keep this scope tight: the ScopeTimer must destruct before
        // SIMPLE_PROFILE_FRAME_END below, otherwise the 60-frame stats print
        // (blocking terminal I/O) gets billed to "Broadcast State".
        SIMPLE_PROFILE_SCOPE("Broadcast State");
        std::vector<entt::entity> allEnts =
            game.gameStateManager.currentState()->getStateEntities(game);
        std::erase_if(allEnts, [&](entt::entity ent) {
          return !shouldSendFrameUpdate(game.registry, ent);
        });
        if (!allEnts.empty()) {
          auto buf = serializeEntities(game.registry, game.componentRegistry,
                                       shared::PacketType::UPDATE_ENTITY,
                                       allEnts, false);
          // Per-tick full snapshots are newest-wins: send unreliable (ENet
          // flag=0 is unreliable-sequenced, so stale/out-of-order snapshots
          // are dropped on the receiver). Sending these reliably head-of-line
          // blocks the channel on any lost fragment, causing client stutter
          // and an intermittent server-side enqueue spike as the reliable
          // queue backs up. Channel 1 keeps this stream off the reliable
          // control channel (0).
          net::broadcastRaw(network.getHost(), buf.data(), buf.size(),
                            /*reliable=*/false, /*channel=*/1);
        }
      }
      SIMPLE_PROFILE_FRAME_END("Server");
      SIMPLE_PROFILE_FRAME_START();
    }

    // Yield so the loop doesn't spin-lock at 100% CPU.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  network.shutdown();
  return EXIT_SUCCESS;
}
