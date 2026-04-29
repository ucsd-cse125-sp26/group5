#include "game_state.h"

#include <cstdio>
#include <memory>
#include <vector>

#include "server_game.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"

// ── GameStateManager ─────────────────────────────────────

void GameStateManager::changeState(ServerGame& game,
                                   std::unique_ptr<IGameState> newState) {
  if (currentState_) currentState_->onExit(game);
  currentState_ = std::move(newState);
  if (currentState_) currentState_->onEnter(game);
}

void GameStateManager::requestStateChange(
    std::unique_ptr<IGameState> newState) {
  pendingState_ = std::move(newState);
}

void GameStateManager::update(ServerGame& game, float dt) {
  if (currentState_) {
    currentState_->update(game, dt);
  }
  if (pendingState_) {
    if (currentState_) currentState_->onExit(game);
    currentState_ = std::move(pendingState_);
    if (currentState_) currentState_->onEnter(game);
  }
}

// ── Helpers ──────────────────────────────────────────────

template <typename Tag>
static void despawnTaggedEntities(ServerGame& game) {
  auto view = game.registry.view<Tag, shared::Entity>();
  for (auto ent : view) {
    uint32_t eid = game.registry.get<shared::Entity>(ent).id;
    shared::DespawnPacket pkt;
    pkt.type = shared::PacketType::DESPAWN_ENTITY;
    pkt.entityId = eid;
    net::broadcastPacket(game.network->getHost(), pkt);
  }
}

template <typename Tag>
static uint32_t findLightEntityId(ServerGame& game) {
  auto view = game.registry.view<Tag, shared::PointLight, shared::Entity>();
  for (auto e : view) {
    return view.template get<shared::Entity>(e).id;
  }
  return kInvalidEntityId;
}

static void broadcastSpawn(ServerGame& game,
                           const std::vector<entt::entity>& entities) {
  if (entities.empty()) return;
  auto buf =
      serializeEntities(game.registry, game.componentRegistry,
                        shared::PacketType::SPAWN_ENTITY, entities, false);
  net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
}

template <typename Tag>
static void clearTaggedPlayerControls(ServerGame& game) {
  auto view = game.registry.view<Tag, shared::PlayerInput>();
  for (auto ent : view) {
    auto& input = view.template get<shared::PlayerInput>(ent);
    input.keys = 0;
    input.keys_prev = 0;
    input.keys_newly_pressed = 0;
    input.mouseDx = 0.0f;
    input.mouseDy = 0.0f;

    if (game.registry.all_of<shared::Velocity>(ent)) {
      auto& velocity = game.registry.get<shared::Velocity>(ent);
      velocity.dx = 0.0f;
      velocity.dy = 0.0f;
      velocity.dz = 0.0f;
    }
  }
}

// ── Initialization ───────────────────────────────────────

void initWorldEntities(ServerGame& game) {
  // --- Overworld Map ---
  {
    auto [eid, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, 5.0f, 0.0f, 3.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "light_cube", 0.2f);
    game.registry.emplace<shared::PointLight>(
        ent, 5.0f, 0.0f, 3.0f, 1.0f, 0.09f, 0.032f, 0.1f, 0.1f, 0.1f, 0.8f,
        0.8f, 0.8f, 1.0f, 1.0f, 1.0f);
    game.registry.emplace<shared::Scene>(ent, "sunny");
    game.registry.emplace<shared::OverworldTag>(ent);
  }
  {
    auto [eid, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, 0.0f, 0.0f, -50.5f, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "cube", 100.0f);
    game.registry.emplace<shared::OverworldTag>(ent);
  }
  struct Decor {
    float x, y, z, scale;
  };
  for (auto& d : (Decor[]){{.x = 5, .y = 5, .z = 0.5, .scale = 1},
                           {.x = -5, .y = 3, .z = 0.5, .scale = 1.5},
                           {.x = 3, .y = -7, .z = 0.5, .scale = 0.8},
                           {.x = -8, .y = -4, .z = 0.5, .scale = 2}}) {
    auto [eid, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, d.x, d.y, d.z, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "cube", d.scale);
    game.registry.emplace<shared::OverworldTag>(ent);
  }

  // --- Maze Map ---
  {
    auto [eid, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, 5.0f, 0.0f, 3.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "light_cube", 0.2f);
    game.registry.emplace<shared::PointLight>(
        ent, 5.0f, 0.0f, 3.0f, 1.0f, 0.09f, 0.032f, 0.1f, 0.1f, 0.1f, 0.8f,
        0.8f, 0.8f, 1.0f, 1.0f, 1.0f);
    game.registry.emplace<shared::Scene>(ent, "night");
    game.registry.emplace<shared::MazeTag>(ent);
  }
  {
    auto [eid, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, 0.0f, 0.0f, -50.5f, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "cube", 100.0f);
    game.registry.emplace<shared::MazeTag>(ent);
  }
  struct BearPos {
    float x, y, z, scale;
  };
  for (auto& b : (BearPos[]){{.x = 3, .y = 0, .z = 0, .scale = 0.1},
                             {.x = -3, .y = 0, .z = 0, .scale = 0.1},
                             {.x = 0, .y = 5, .z = 0, .scale = 0.2},
                             {.x = 0, .y = -5, .z = 0, .scale = 0.2},
                             {.x = 6, .y = 6, .z = 0, .scale = 0.15},
                             {.x = -6, .y = -6, .z = 0, .scale = 0.15}}) {
    auto [eid, ent] = new_entity(game);
    game.registry.emplace<shared::Position>(ent, b.x, b.y, b.z, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ent, "bear", b.scale);
    game.registry.emplace<shared::MazeTag>(ent);
  }

  // --- Pool slots ---
  for (int i = 0; i < 4; i++) {
    PlayerAvatars slots;
    auto [oeid, oent] = new_entity(game);
    game.registry.emplace<shared::Position>(oent, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::Velocity>(oent, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(oent, "cube", 1.0f);
    game.registry.emplace<shared::Camera>(oent, 0.0f, 1.0f);
    game.registry.emplace<shared::PlayerInput>(
        oent, static_cast<InputKeys>(0), static_cast<InputKeys>(0),
        static_cast<InputKeys>(0), 0.0f, 0.0f);
    game.registry.emplace<shared::OverworldTag>(oent);
    slots.overworld_avatar = oent;

    auto [meid, ment] = new_entity(game);
    game.registry.emplace<shared::Position>(ment, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f);
    game.registry.emplace<shared::Velocity>(ment, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(ment, "bear", 0.5f);
    game.registry.emplace<shared::Camera>(ment, 0.0f, 1.0f);
    game.registry.emplace<shared::PlayerInput>(
        ment, static_cast<InputKeys>(0), static_cast<InputKeys>(0),
        static_cast<InputKeys>(0), 0.0f, 0.0f);
    game.registry.emplace<shared::MazeTag>(ment);
    slots.maze_avatar = ment;

    game.unused_player_slots.push_back(slots);
  }
}

// ── OverworldState ───────────────────────────────────────

void OverworldState::onEnter(ServerGame& game) {
  printf("[State] Entering Overworld\n");

  std::vector<entt::entity> spawned;
  auto view = game.registry.view<shared::OverworldTag>();
  for (auto ent : view) {
    spawned.push_back(ent);
  }

  // Update client authority IDs
  for (auto& pair : game.active_players) {
    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId =
        game.registry.get<shared::Entity>(pair.second.overworld_avatar).id;
    net::sendPacket(pair.first, assignPkt);
  }

  broadcastSpawn(game, spawned);
}

void OverworldState::onExit(ServerGame& game) {
  printf("[State] Exiting Overworld\n");
  clearTaggedPlayerControls<shared::OverworldTag>(game);
  despawnTaggedEntities<shared::OverworldTag>(game);
}

void OverworldState::update(ServerGame& game, float dt) {
  input_tick(game.registry);

  // Press 1 → enter maze
  auto inputView =
      game.registry.view<shared::PlayerInput, shared::OverworldTag>();
  for (auto ent : inputView) {
    auto& input = game.registry.get<shared::PlayerInput>(ent);
    if (input.keys_newly_pressed & KEY_ENTER_MAZE) {
      game.gameStateManager.requestStateChange(std::make_unique<MazeState>());
      return;
    }
  }

  movement_system(game.registry, dt, StateType::OVERWORLD);
  render_model_change(game.registry, dt);

  uint32_t lightId = findLightEntityId<shared::OverworldTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry);
}

// ── MazeState ────────────────────────────────────────────

void MazeState::onEnter(ServerGame& game) {
  printf("[State] Entering Maze\n");

  std::vector<entt::entity> spawned;
  auto view = game.registry.view<shared::MazeTag>();
  for (auto ent : view) {
    spawned.push_back(ent);
  }

  // Update client authority IDs
  for (auto& pair : game.active_players) {
    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId =
        game.registry.get<shared::Entity>(pair.second.maze_avatar).id;
    net::sendPacket(pair.first, assignPkt);
  }

  broadcastSpawn(game, spawned);
}

void MazeState::onExit(ServerGame& game) {
  printf("[State] Exiting Maze\n");
  clearTaggedPlayerControls<shared::MazeTag>(game);
  despawnTaggedEntities<shared::MazeTag>(game);
}

void MazeState::update(ServerGame& game, float dt) {
  input_tick(game.registry);

  // Press Q → back to overworld
  auto inputView = game.registry.view<shared::PlayerInput, shared::MazeTag>();
  for (auto ent : inputView) {
    auto& input = game.registry.get<shared::PlayerInput>(ent);
    if (input.keys_newly_pressed & KEY_EXIT_MINIGAME) {
      game.gameStateManager.requestStateChange(
          std::make_unique<OverworldState>());
      return;
    }
  }

  movement_system(game.registry, dt, StateType::MAZE);
  render_model_change(game.registry, dt);

  uint32_t lightId = findLightEntityId<shared::MazeTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry);
}
