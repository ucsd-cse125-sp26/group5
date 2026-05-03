#include "game_state.h"

#include <cstdio>
#include <memory>
#include <vector>

#include "scene.h"
#include "server_game.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/util.h"

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

template <typename Tag>
static void addPhysicsBodies(ServerGame& game) {
  auto view = game.registry.view<Tag, shared::PhysicsBody>();
  auto& bodyInterface = game.physics.getBodyInterface();
  for (auto ent : view) {
    auto& phys = view.template get<shared::PhysicsBody>(ent);
    JPH::BodyID bodyId(phys.bodyId);
    if (!bodyInterface.IsAdded(bodyId)) {
      bodyInterface.AddBody(bodyId, JPH::EActivation::DontActivate);
    }
  }
}

template <typename Tag>
static void removePhysicsBodies(ServerGame& game) {
  auto view = game.registry.view<Tag, shared::PhysicsBody>();
  auto& bodyInterface = game.physics.getBodyInterface();
  for (auto ent : view) {
    auto& phys = view.template get<shared::PhysicsBody>(ent);
    JPH::BodyID bodyId(phys.bodyId);
    if (bodyInterface.IsAdded(bodyId)) {
      bodyInterface.RemoveBody(bodyId);
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

    spawnStaticEntitiesForWorld<shared::OverworldTag>(
        game,
        {
            {.x = 5.0f,
             .y = 5.0f,
             .z = 0.0f,
             .modelName = "cube",
             .scale = 1.0f,
             .meshPath = "",
             .render = true},
            {.x = 10.0f,
             .y = 0.0f,
             .z = -1.0f,
             .modelName = "bear",
             .scale = 0.5f,
             .meshPath = (exeDir() / "assets/bear/bear_full.obj").string(),
             .render = true},
            {.x = 0.0f,
             .y = 0.0f,
             .z = -1.0f,
             .modelName = "floor",
             .scale = 1.0f,
             .meshPath = "",
             .render = false,
             .halfX = 100.0f,
             .halfY = 100.0f,
             .halfZ = 1.0f},
            {.x = 5.0f,
             .y = 5.0f,
             .z = 0.5f,
             .modelName = "cube",
             .scale = 1.0f,
             .meshPath = "",
             .render = true},
            {.x = -5.0f,
             .y = 3.0f,
             .z = 0.5f,
             .modelName = "cube",
             .scale = 1.5f,
             .meshPath = "",
             .render = true},
            {.x = 3.0f,
             .y = -7.0f,
             .z = 0.5f,
             .modelName = "cube",
             .scale = 0.8f,
             .meshPath = "",
             .render = true},
            {.x = -8.0f,
             .y = -4.0f,
             .z = 0.5f,
             .modelName = "cube",
             .scale = 2.0f,
             .meshPath = "",
             .render = true},
        });
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

    const std::string bearMesh =
        (exeDir() / "assets/bear/bear_full.obj").string();
    spawnStaticEntitiesForWorld<shared::MazeTag>(game,
                                                 {
                                                     {.x = 0.0f,
                                                      .y = 0.0f,
                                                      .z = -1.0f,
                                                      .modelName = "floor",
                                                      .scale = 1.0f,
                                                      .meshPath = "",
                                                      .render = false,
                                                      .halfX = 100.0f,
                                                      .halfY = 100.0f,
                                                      .halfZ = 1.0f},
                                                     {.x = 3.0f,
                                                      .y = 0.0f,
                                                      .z = 0.0f,
                                                      .modelName = "bear",
                                                      .scale = 0.1f,
                                                      .meshPath = bearMesh,
                                                      .render = true},
                                                     {.x = -3.0f,
                                                      .y = 0.0f,
                                                      .z = 0.0f,
                                                      .modelName = "bear",
                                                      .scale = 0.1f,
                                                      .meshPath = bearMesh,
                                                      .render = true},
                                                     {.x = 0.0f,
                                                      .y = 5.0f,
                                                      .z = 0.0f,
                                                      .modelName = "bear",
                                                      .scale = 0.2f,
                                                      .meshPath = bearMesh,
                                                      .render = true},
                                                     {.x = 0.0f,
                                                      .y = -5.0f,
                                                      .z = 0.0f,
                                                      .modelName = "bear",
                                                      .scale = 0.2f,
                                                      .meshPath = bearMesh,
                                                      .render = true},
                                                     {.x = 6.0f,
                                                      .y = 6.0f,
                                                      .z = 0.0f,
                                                      .modelName = "bear",
                                                      .scale = 0.15f,
                                                      .meshPath = bearMesh,
                                                      .render = true},
                                                     {.x = -6.0f,
                                                      .y = -6.0f,
                                                      .z = 0.0f,
                                                      .modelName = "bear",
                                                      .scale = 0.15f,
                                                      .meshPath = bearMesh,
                                                      .render = true},
                                                 });
  }

  // --- Pool slots ---
  for (int i = 0; i < 4; i++) {
    float startX = i * 10.0f;  // Hardcode spread out to prevent overlap
    PlayerAvatars slots;

    auto [overworldEntityId, overworldEntity] = new_entity(game);
    game.registry.emplace<shared::Position>(overworldEntity, startX, 0.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::Velocity>(overworldEntity, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(overworldEntity, "cube", 1.0f);
    game.registry.emplace<shared::Camera>(overworldEntity, 0.0f, 1.0f);
    game.registry.emplace<shared::PlayerInput>(
        overworldEntity, InputKeys(0), InputKeys(0), InputKeys(0), 0.0f, 0.0f);
    game.registry.emplace<shared::OverworldTag>(overworldEntity);
    JPH::BodyID overworldBodyId =
        game.physics.createPlayerBody(startX, 0.0f, 0.0f);
    game.registry.emplace<shared::PhysicsBody>(
        overworldEntity, overworldBodyId.GetIndexAndSequenceNumber());
    slots.overworld_avatar = overworldEntity;

    auto [mazeEntityId, mazeEntity] = new_entity(game);
    game.registry.emplace<shared::Position>(mazeEntity, startX, 0.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::Velocity>(mazeEntity, 0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(mazeEntity, "bear", 0.5f);
    game.registry.emplace<shared::Camera>(mazeEntity, 0.0f, 1.0f);
    game.registry.emplace<shared::PlayerInput>(
        mazeEntity, InputKeys(0), InputKeys(0), InputKeys(0), 0.0f, 0.0f);
    game.registry.emplace<shared::MazeTag>(mazeEntity);
    JPH::BodyID mazeBodyId = game.physics.createPlayerBody(startX, 0.0f, 0.0f);
    game.registry.emplace<shared::PhysicsBody>(
        mazeEntity, mazeBodyId.GetIndexAndSequenceNumber());
    slots.maze_avatar = mazeEntity;

    game.unused_player_slots.push_back(slots);
  }

  // Remove all state-owned physics bodies after creation. State enter will add
  // back only the active world's bodies.
  removePhysicsBodies<shared::MazeTag>(game);
  removePhysicsBodies<shared::OverworldTag>(game);
}

// ── Delegating Helpers ───────────────────────────────────

template <typename Tag, entt::entity PlayerAvatars::* AvatarField>
static void enterStateHelper(ServerGame& game, const char* stateName) {
  printf("[State] Entering %s\n", stateName);
  std::vector<entt::entity> spawned;
  auto view = game.registry.view<Tag>();
  for (auto ent : view) spawned.push_back(ent);

  for (auto& pair : game.active_players) {
    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    entt::entity target_avatar = pair.second.*AvatarField;
    assignPkt.entityId = game.registry.get<shared::Entity>(target_avatar).id;
    net::sendPacket(pair.first, assignPkt);
  }
  broadcastSpawn(game, spawned);
}

template <typename Tag>
static std::vector<entt::entity> getEntitiesHelper(ServerGame& game) {
  std::vector<entt::entity> existing;
  auto view = game.registry.view<Tag>();
  for (auto ent : view) existing.push_back(ent);
  return existing;
}

// ── OverworldState ───────────────────────────────────────

void OverworldState::onEnter(ServerGame& game) {
  addPhysicsBodies<shared::OverworldTag>(game);
  enterStateHelper<shared::OverworldTag, &PlayerAvatars::overworld_avatar>(
      game, "Overworld");
}

void OverworldState::onExit(ServerGame& game) {
  printf("[State] Exiting Overworld\n");
  removePhysicsBodies<shared::OverworldTag>(game);
  clearTaggedPlayerControls<shared::OverworldTag>(game);
  despawnTaggedEntities<shared::OverworldTag>(game);
}

entt::entity OverworldState::getClientAvatar(const PlayerAvatars& slots) const {
  return slots.overworld_avatar;
}

std::vector<entt::entity> OverworldState::getStateEntities(
    ServerGame& game) const {
  return getEntitiesHelper<shared::OverworldTag>(game);
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

  movement_system(game, dt, StateType::OVERWORLD);
  render_model_change(game.registry, dt);

  uint32_t lightId = findLightEntityId<shared::OverworldTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry);
}

// ── MazeState ────────────────────────────────────────────

void MazeState::onEnter(ServerGame& game) {
  addPhysicsBodies<shared::MazeTag>(game);
  enterStateHelper<shared::MazeTag, &PlayerAvatars::maze_avatar>(game, "Maze");
}

void MazeState::onExit(ServerGame& game) {
  printf("[State] Exiting Maze\n");
  removePhysicsBodies<shared::MazeTag>(game);
  clearTaggedPlayerControls<shared::MazeTag>(game);
  despawnTaggedEntities<shared::MazeTag>(game);
}

entt::entity MazeState::getClientAvatar(const PlayerAvatars& slots) const {
  return slots.maze_avatar;
}

std::vector<entt::entity> MazeState::getStateEntities(ServerGame& game) const {
  return getEntitiesHelper<shared::MazeTag>(game);
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

  movement_system(game, dt, StateType::MAZE);
  render_model_change(game.registry, dt);

  uint32_t lightId = findLightEntityId<shared::MazeTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry);
}
