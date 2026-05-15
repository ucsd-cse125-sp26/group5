#include "game_state.h"

#include <cstdio>
#include <memory>
#include <vector>

#include "game/maze.h"
#include "game/maze_generation.h"
#include "game/maze_trigger.h"
#include "game/overworld.h"
#include "map_loader.h"
#include "scene.h"
#include "server_game.h"
#include "server_level_loader.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/lighting.h"
#include "shared/map_format.h"
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

// Find the demo light by looking for a PointLight with a RenderInfo (the cube
// marker). Map-loaded point lights have no RenderInfo, so this filter excludes
// them — without it EnTT's newest-first iteration returns a map light and
// hardcoded_spinning_light moves the wrong entity.
template <typename Tag>
static uint32_t findLightEntityId(ServerGame& game) {
  auto view =
      game.registry
          .view<Tag, shared::PointLight, shared::RenderInfo, shared::Entity>();
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

namespace {

constexpr int kGeneratedMazeWidth = 8;
constexpr int kGeneratedMazeHeight = 8;
constexpr uint32_t kGeneratedMazeSeed = 12505;
constexpr float kMazeTileSpacing = 1.5f;

struct GeneratedMazeData {
  maze::MazeLayout layout;
  maze::MazeTileGrid tileGrid;
};

GeneratedMazeData buildGeneratedMazeData() {
  GeneratedMazeData data;
  data.layout = maze::GenerateMazeLayout(kGeneratedMazeWidth, kGeneratedMazeHeight,
                                         kGeneratedMazeSeed);
  data.tileGrid = maze::ConvertToTileGrid(data.layout);
  return data;
}

template <typename Tag>
void spawnDemoLight(ServerGame& game, const char* sceneName) {
  auto [eid, ent] = new_entity(game);
  game.registry.emplace<shared::Position>(ent, 5.0f, 0.0f, 3.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(ent, "light_cube", 0.2f, 0.2f,
                                            0.2f);
  constexpr auto kAtt = shared::kDefaultPointLightAttenuation;
  game.registry.emplace<shared::PointLight>(
      ent, 5.0f, 0.0f, 3.0f, kAtt.constant, kAtt.linear, kAtt.quadratic, 0.1f,
      0.1f, 0.1f, 0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f);
  game.registry.emplace<shared::Scene>(ent, sceneName);
  game.registry.emplace<Tag>(ent);
}

template <typename Tag>
void spawnPlayerAvatar(ServerGame& game, entt::entity entity,
                       const std::string& modelName, const glm::vec3& pos,
                       const glm::vec3& scale) {
  game.registry.emplace<shared::Position>(entity, pos.x, pos.y, pos.z, 1.0f,
                                          0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::Velocity>(entity, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(entity, modelName, scale.x, scale.y,
                                            scale.z);
  game.registry.emplace<shared::Camera>(entity, 0.0f, 1.0f);
  game.registry.emplace<shared::PlayerInput>(entity, InputKeys(0), InputKeys(0),
                                             InputKeys(0), 0.0f, 0.0f);
  game.registry.emplace<Tag>(entity);
  game.registry.emplace<shared::ColorBoundingBox>(entity);
  {
    auto& box = game.registry.get<shared::ColorBoundingBox>(entity);
    box.minX = 40.0f;
    box.minY = 25.0f;
    box.minZ = -500.0f;
    box.maxX = 90.0f;
    box.maxY = 55.0f;
    box.maxZ = 500.0f;
  }
  JPH::BodyID bodyId = game.physics.createPlayerBody(
      modelName, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), scale);
  game.registry.emplace<shared::PhysicsBody>(
      entity, bodyId.GetIndexAndSequenceNumber());
}

std::vector<StaticEntityDesc> buildGeneratedMazeEntities() {
  const GeneratedMazeData data = buildGeneratedMazeData();

  std::vector<StaticEntityDesc> entities;
  entities.push_back(StaticEntityDesc{.position = glm::vec3(0.0f, 0.0f, -50.0f),
                                      .modelName = "cube",
                                      .scale = glm::vec3(100.0f)});

  for (int y = 0; y < data.tileGrid.height; ++y) {
    for (int x = 0; x < data.tileGrid.width; ++x) {
      if (data.tileGrid.Tile(x, y) != maze::MazeTile::Wall) continue;

      entities.push_back(StaticEntityDesc{
          .position =
              glm::vec3((static_cast<float>(x) - 1.0f) * kMazeTileSpacing,
                        (static_cast<float>(y) - 1.0f) * kMazeTileSpacing, 0.75f),
          .modelName = "cube",
          .scale = glm::vec3(0.7f, 0.7f, 1.5f),
      });
    }
  }

  return entities;
}

std::vector<StaticEntityDesc> buildOverworldMazePreviewEntities() {
  const GeneratedMazeData data = buildGeneratedMazeData();
  constexpr glm::vec3 kPreviewCenter =
      glm::vec3(maze_trigger::kCenterX, maze_trigger::kCenterY, 4.5f);
  constexpr float kPreviewTileSpacing = 0.18f;
  constexpr float kFloorDepthOffset = -0.20f;
  constexpr float kWallDepthOffset = -0.08f;
  constexpr glm::vec3 kFloorScale = glm::vec3(0.18f, 0.02f, 0.18f);
  constexpr glm::vec3 kWallScale = glm::vec3(0.18f, 0.12f, 0.18f);
  constexpr float kMarkerDepthOffset = -0.30f;
  constexpr glm::vec3 kStartMarkerScale = glm::vec3(0.24f, 0.16f, 0.24f);
  constexpr glm::vec3 kGoalMarkerScale = glm::vec3(0.28f, 0.16f, 0.28f);

  const float xOffset =
      (static_cast<float>(data.tileGrid.width) - 1.0f) * 0.5f;
  const float yOffset =
      (static_cast<float>(data.tileGrid.height) - 1.0f) * 0.5f;

  auto previewPosition = [&](int x, int y, float yOffsetFromBoard) {
    return glm::vec3(kPreviewCenter.x +
                         (static_cast<float>(x) - xOffset) * kPreviewTileSpacing,
                     kPreviewCenter.y + yOffsetFromBoard,
                     kPreviewCenter.z +
                         (yOffset - static_cast<float>(y)) * kPreviewTileSpacing);
  };

  std::vector<StaticEntityDesc> entities;

  for (int y = 0; y < data.tileGrid.height; ++y) {
    for (int x = 0; x < data.tileGrid.width; ++x) {
      const bool isWall = data.tileGrid.Tile(x, y) == maze::MazeTile::Wall;

      entities.push_back(StaticEntityDesc{
          .position =
              previewPosition(x, y, isWall ? kWallDepthOffset : kFloorDepthOffset),
          .modelName = isWall ? "light_cube" : "cube",
          .scale = isWall ? kWallScale : kFloorScale,
      });
    }
  }

  const int startTileX = data.layout.startX * 2 + 1;
  const int startTileY = data.layout.startY * 2 + 1;
  const int goalTileX = data.layout.goalX * 2 + 1;
  const int goalTileY = data.layout.goalY * 2 + 1;
  entities.push_back(StaticEntityDesc{
      .position = previewPosition(startTileX, startTileY, kMarkerDepthOffset),
      .modelName = "start_cube",
      .scale = kStartMarkerScale,
  });
  entities.push_back(StaticEntityDesc{
      .position = previewPosition(goalTileX, goalTileY, kMarkerDepthOffset),
      .modelName = "goal_cube",
      .scale = kGoalMarkerScale,
  });

  return entities;
}

}  // namespace

void initWorldEntities(ServerGame& game) {
  // --- Overworld ---
  spawnDemoLight<shared::OverworldTag>(game, "sunny");
  loadMap<shared::OverworldTag>(game,
                                (exeDir() / shared::DEFAULT_MAP_PATH).string());
  spawnStaticEntities<shared::OverworldTag>(
      game, {
                // 100³ floor cube; top surface lands on z=0.
                StaticEntityDesc{.position = glm::vec3(0.0f, 0.0f, -50.0f),
                                 .modelName = "cube",
                                 .scale = glm::vec3(100.0f)},
                StaticEntityDesc{.position = glm::vec3(5.0f, 5.0f, 0.5f),
                                 .modelName = "cube"},
                StaticEntityDesc{.position = glm::vec3(-5.0f, 3.0f, 0.5f),
                                 .modelName = "cube",
                                 .scale = glm::vec3(1.5f)},
                StaticEntityDesc{.position = glm::vec3(3.0f, -7.0f, 0.5f),
                                 .modelName = "cube",
                                 .scale = glm::vec3(0.8f)},
                StaticEntityDesc{.position = glm::vec3(-8.0f, -4.0f, 0.5f),
                                 .modelName = "cube",
                                 .scale = glm::vec3(2.0f)},
                StaticEntityDesc{.position = glm::vec3(10.0f, 0.0f, 0.0f),
                                 .modelName = "bear",
                                 .scale = glm::vec3(0.5f),
                                 .collision = CollisionShape::Box},
                StaticEntityDesc{.position = glm::vec3(20.0f, 0.0f, 0.0f),
                                 .modelName = "bear",
                                 .scale = glm::vec3(0.5f),
                                 .collision = CollisionShape::Mesh},
            });
  spawnStaticEntities<shared::OverworldTag>(game,
                                            buildOverworldMazePreviewEntities());
  spawnStaticEntities<shared::OverworldTag>(
      game, maze_trigger::buildMazeTriggerMarkerEntities());

  // --- Maze ---
  spawnDemoLight<shared::MazeTag>(game, "night");
  spawnStaticEntities<shared::MazeTag>(game, buildGeneratedMazeEntities());

  // --- Pool slots ---
  for (int i = 0; i < 4; i++) {
    float startX = i * 10.0f;  // Hardcode spread out to prevent overlap
    PlayerAvatars slots;

    auto [overworldEntityId, overworldEntity] = new_entity(game);
    spawnPlayerAvatar<shared::OverworldTag>(game, overworldEntity, "cube",
                                            glm::vec3(startX, 0.0f, 0.0f),
                                            glm::vec3(1.0f));
    slots.overworld_avatar = overworldEntity;

    auto [mazeEntityId, mazeEntity] = new_entity(game);
    spawnPlayerAvatar<shared::MazeTag>(game, mazeEntity, "bear",
                                       glm::vec3(startX, 0.0f, 0.0f),
                                       glm::vec3(0.5f));
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

  const bool allInTrigger = maze_trigger::allActivePlayersInMazeTrigger(game);
  if (!allInTrigger) {
    game.overworldMazeTriggerArmed = true;
  } else if (game.overworldMazeTriggerArmed) {
    game.overworldMazeTriggerArmed = false;
    game.gameStateManager.requestStateChange(std::make_unique<MazeState>());
    return;
  }

  movement_system(game, dt, StateType::OVERWORLD);
  render_model_change(game, dt);

  uint32_t lightId = findLightEntityId<shared::OverworldTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry, StateType::OVERWORLD);
}

// ── MazeState ────────────────────────────────────────────

void MazeState::onEnter(ServerGame& game) {
  addPhysicsBodies<shared::MazeTag>(game);
  ResetMazeSpiritSpawn(game);

  auto& bodyInterface = game.physics.getBodyInterface();
  auto mazeInputBodies =
      game.registry
          .view<shared::MazeTag, shared::PhysicsBody, shared::PlayerInput>();
  for (auto ent : mazeInputBodies) {
    if (game.registry.all_of<shared::MazeSpiritGrid>(ent)) continue;
    auto& pb = game.registry.get<shared::PhysicsBody>(ent);
    JPH::BodyID id(pb.bodyId);
    if (bodyInterface.IsAdded(id)) {
      bodyInterface.SetLinearVelocity(id, JPH::Vec3::sZero());
    }
  }

  enterStateHelper<shared::MazeTag, &PlayerAvatars::maze_avatar>(game, "Maze");
  EnterMazePuzzle(game);
  if (HasUnlockedWinterSection(game)) {
    ClaimPadsForActivePlayers(game, GetWinterPuzzleNumericId(game));
  }
}

void MazeState::onExit(ServerGame& game) {
  printf("[State] Exiting Maze\n");
  game.overworldMazeTriggerArmed = false;
  ExitMazePuzzle(game);
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
  TickMazeExploration(game, dt);

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

  // Maze uses shared-cube arrow logic; keep per-player WASD movement disabled
  // here.
  render_model_change(game, dt);

  uint32_t lightId = findLightEntityId<shared::MazeTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry, StateType::MAZE);
}
