#include "game_state.h"

#include <cstdio>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "game/maze.h"
#include "server/game/puzzles/maze/camera.h"
#include "game/maze_generation.h"
#include "server/game/puzzles/maze/layout_editor.h"
#include "server/game/puzzles/maze/trigger.h"
#include "game/overworld.h"
#include "server/game/puzzles/maze/puzzle.h"
#include "server/game/puzzles/tangram/puzzle.h"
#include "server/game/puzzles/tangram/layout_editor.h"
#include "server/game/puzzles/tangram/trigger.h"
#include "server/game/puzzles/tangram/camera.h"
#include "map_loader.h"
#include "scene.h"
#include "server_game.h"
#include "server_level_loader.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/dev_spawn.h"
#include "shared/lighting.h"
#include "shared/map_format.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/input.h"
#include "shared/sound_constants.h"
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
    input.rotateTargetId = 0;

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
    // Unassigned pool avatars (playerSlot==0) stay out of the sim until connect.
    if (game.registry.all_of<shared::PlayerInput, shared::RenderInfo>(ent)) {
      const uint8_t slot =
          game.registry.get<shared::RenderInfo>(ent).playerSlot;
      if (slot == 0) continue;
    }
    auto& phys = view.template get<shared::PhysicsBody>(ent);
    JPH::BodyID bodyId(phys.bodyId);
    if (!bodyInterface.IsAdded(bodyId)) {
      bodyInterface.AddBody(bodyId, JPH::EActivation::DontActivate);
    }
    // Prime wasGrounded=true so the first grounded tick doesn't look like a
    // landing
    if (game.registry.all_of<shared::Grounded>(ent)) {
      auto& g = game.registry.get<shared::Grounded>(ent);
      g.wasGrounded = true;
      g.isGrounded = true;
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

static void debugPrintRequestedPlayerPosition(ServerGame& game) {
  auto view =
      game.registry.view<shared::OverworldTag, shared::PlayerInput,
                         shared::Position, shared::RenderInfo>();
  for (auto ent : view) {
    const auto& input = game.registry.get<shared::PlayerInput>(ent);
    if ((input.keys_newly_pressed & KEY_DEBUG_PRINT_POS) == 0) continue;

    const auto& pos = game.registry.get<shared::Position>(ent);
    const auto& ri = game.registry.get<shared::RenderInfo>(ent);
    printf(
        "[DebugPos] slot=%u player=(%.3f, %.3f, %.3f) "
        "maze_board_suggest=(%.3f, %.3f, %.3f)\n",
        static_cast<unsigned>(ri.playerSlot), pos.x, pos.y, pos.z, pos.x, pos.y,
        pos.z);
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
  data.layout = maze::GenerateMazeLayout(
      kGeneratedMazeWidth, kGeneratedMazeHeight, kGeneratedMazeSeed);
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
  game.registry.emplace<shared::Grounded>(entity);
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
          .position = glm::vec3(
              (static_cast<float>(x) - 1.0f) * kMazeTileSpacing,
              (static_cast<float>(y) - 1.0f) * kMazeTileSpacing, 0.75f),
          .modelName = "cube",
          .scale = glm::vec3(0.7f, 0.7f, 1.5f),
      });
    }
  }

  const int goalTileX = data.layout.goalX * 2 + 1;
  const int goalTileY = data.layout.goalY * 2 + 1;
  entities.push_back(StaticEntityDesc{
      .position = glm::vec3(
          (static_cast<float>(goalTileX) - 1.0f) * kMazeTileSpacing,
          (static_cast<float>(goalTileY) - 1.0f) * kMazeTileSpacing, 0.9f),
      .modelName = "goal_cube",
      .scale = glm::vec3(0.7f, 0.7f, 0.7f),
      .collision = CollisionShape::None,
  });

  return entities;
}

}  // namespace

void initWorldEntities(ServerGame& game) {
  // --- Overworld ---
  spawnDemoLight<shared::OverworldTag>(game, "sunny");
  game.tangramArena = shared::tangram::ArenaLayout::defaults();
  loadMap<shared::OverworldTag>(game,
                                (exeDir() / shared::DEFAULT_MAP_PATH).string());
  spawnStaticEntities<shared::OverworldTag>(
      game,
      {
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
          StaticEntityDesc{
              .position = glm::vec3(10.0f, 0.0f, 0.0f),
              .modelName = "bear",
              .scale = glm::vec3(0.5f),
              .collision = CollisionShape::Box,
              .soundLayers =
                  {
                      shared::SoundLayer{
                          .soundId = static_cast<uint32_t>(
                              shared::SoundId::AMBIENT_HUM),
                          .trigger = shared::SoundTriggerType::ALWAYS,
                          .volume = 0.5f},
                  }},
          StaticEntityDesc{.position = glm::vec3(20.0f, 0.0f, 0.0f),
                           .modelName = "bear",
                           .scale = glm::vec3(0.5f),
                           .collision = CollisionShape::Mesh},
      });
  maze_puzzle::initOverworldMazePuzzleController(game);
  maze_layout_editor::spawnLayoutVisuals(game);
  tangram_puzzle::initController(game);
  tangram_layout_editor::spawnLayoutVisuals(game);

  // Section barriers — TODO: replace positions/sizes once you have map coords
  spawnSectionBarrier<shared::OverworldTag>(
      game,
      /*sectionID=*/0,
      /*season=*/shared::SectionSeasonMap::WINTER,
      /*pos=*/glm::vec3(90.0f, 47.5f, 0.0f),
      /*halfExtents=*/glm::vec3(1.0f, 58.0f, 100.0f));
  spawnSectionBarrier<shared::OverworldTag>(
      game,
      /*sectionID=*/1,
      /*season=*/shared::SectionSeasonMap::FALL,
      /*pos=*/glm::vec3(92.5f, -10.0f, 0.0f),
      /*halfExtents=*/glm::vec3(82.0f, 1.0f, 100.0f));
  spawnSectionBarrier<shared::OverworldTag>(
      game,
      /*sectionID=*/2,
      /*season=*/shared::SectionSeasonMap::SUMMER,
      /*pos=*/glm::vec3(10.0f, 0.0f, 0.0f),
      /*halfExtents=*/glm::vec3(1.0f, 105.0f, 100.0f));
  // spawnSectionBarrier<shared::OverworldTag>(game,
  //     /*sectionID=*/3,
  //     /*season=*/shared::SectionSeasonMap::SPRING,
  //     /*pos=*/glm::vec3(0.0f, 0.0f, 0.0f),
  //     /*halfExtents=*/glm::vec3(1.0f, 20.0f, 5.0f));
  // add more per section as needed

  // Invisible map boundary walls — actual GLB bounds: X[-169,171] Y[-59,145]
  spawnInvisibleWall<shared::OverworldTag>(
      game, glm::vec3(1.0f, 103.0f, 0.0f),  // north  (Y=105 + buffer)
      glm::vec3(172.0f, 1.0f, 150.0f));
  spawnInvisibleWall<shared::OverworldTag>(
      game, glm::vec3(1.0f, -105.0f, 0.0f),  // south  (Y=-105 - buffer)
      glm::vec3(172.0f, 1.0f, 150.0f));
  spawnInvisibleWall<shared::OverworldTag>(
      game, glm::vec3(170.0f, 1.0f, 0.0f),  // east   (X=171 + buffer)
      glm::vec3(1.0f, 106.0f, 150.0f));
  spawnInvisibleWall<shared::OverworldTag>(
      game, glm::vec3(-170.0f, 1.0f, 0.0f),  // west   (X=-169 - buffer)
      glm::vec3(1.0f, 106.0f, 150.0f));
  // Skinned demo: dancing vampire. No physics body — the DAE is large and
  // a collision proxy isn't useful for a decorative animation test.
  {
    auto [vampireId, vampire] = new_entity(game);
    (void)vampireId;
    game.registry.emplace<shared::Position>(vampire, 5.0f, 0.0f, 0.0f, 1.0f,
                                            0.0f, 0.0f, 0.0f);
    game.registry.emplace<shared::RenderInfo>(vampire, "vampire", 1.0f, 1.0f,
                                              1.0f);
    game.registry.emplace<shared::AnimationState>(vampire, std::string{}, 0u,
                                                  true);
    game.registry.emplace<shared::OverworldTag>(vampire);
  }

  // --- Maze ---
  spawnDemoLight<shared::MazeTag>(game, "night");
  spawnStaticEntities<shared::MazeTag>(game, buildGeneratedMazeEntities());

  // --- Pool slots ---
  for (int i = 0; i < 4; i++) {
    const auto slot = static_cast<uint8_t>(i + 1);
    PlayerAvatars slots;

    auto [overworldEntityId, overworldEntity] = new_entity(game);
    spawnPlayerAvatar<shared::OverworldTag>(
        game, overworldEntity, "cube",
        shared::dev_spawn::overworldSpawnPosition(game.mazeLayout,
                                                  game.tangramArena, slot),
        glm::vec3(1.0f));
    game.registry.get<shared::RenderInfo>(overworldEntity).playerSlot = 0;
    slots.overworld_avatar = overworldEntity;

    auto [mazeEntityId, mazeEntity] = new_entity(game);
    spawnPlayerAvatar<shared::MazeTag>(
        game, mazeEntity, "bear", maze_trigger::overworldSpawnPosition(game, slot),
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
  shared::StateChangePacket pkt;
  pkt.state = shared::GameStateType::OVERWORLD;
  net::broadcastPacket(game.network->getHost(), pkt);

  addPhysicsBodies<shared::OverworldTag>(game);
  for (auto& [peer, slots] : game.active_players) {
    (void)peer;
    uint8_t slot = 1;
    if (game.registry.valid(slots.overworld_avatar) &&
        game.registry.all_of<shared::RenderInfo>(slots.overworld_avatar)) {
      slot = game.registry.get<shared::RenderInfo>(slots.overworld_avatar)
                 .playerSlot;
      if (slot < 1 || slot > 4) slot = 1;
    }
    if (game.registry.valid(slots.overworld_avatar) &&
        game.registry.all_of<shared::Position>(slots.overworld_avatar)) {
      const glm::vec3 spawn = shared::dev_spawn::overworldSpawnPosition(
          game.mazeLayout, game.tangramArena, slot);
      auto& pos = game.registry.get<shared::Position>(slots.overworld_avatar);
      pos.x = spawn.x;
      pos.y = spawn.y;
      pos.z = spawn.z;
      if (game.registry.all_of<shared::Velocity>(slots.overworld_avatar)) {
        auto& vel = game.registry.get<shared::Velocity>(slots.overworld_avatar);
        vel.dx = vel.dy = vel.dz = 0.0f;
      }
      if (game.registry.all_of<shared::PhysicsBody>(slots.overworld_avatar)) {
        auto& pb = game.registry.get<shared::PhysicsBody>(slots.overworld_avatar);
        auto& bi = game.physics.getBodyInterface();
        JPH::BodyID body(pb.bodyId);
        if (bi.IsAdded(body)) {
          bi.SetPosition(body, JPH::RVec3(pos.x, pos.y, pos.z),
                         JPH::EActivation::Activate);
          bi.SetLinearVelocity(body, JPH::Vec3::sZero());
        }
      }
    }
  }
  enterStateHelper<shared::OverworldTag, &PlayerAvatars::overworld_avatar>(
      game, "Overworld");
}

void OverworldState::onExit(ServerGame& game) {
  printf("[State] Exiting Overworld\n");
  if (maze_puzzle::isPuzzleActive(game)) {
    maze_puzzle::endPuzzle(game);
  }
  if (tangram_puzzle::isPuzzleActive(game)) {
    tangram_puzzle::endPuzzle(game);
  }
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
  debugPrintRequestedPlayerPosition(game);

  if (tangram_puzzle::isPuzzleActive(game)) {
    tangram_trigger::keepPlayersOnTangramPlatform(game);
    movement_system(game, dt, StateType::OVERWORLD);
    tangram_puzzle::updatePuzzle(game, dt);
    render_model_change(game, dt);
    uint32_t lightId = findLightEntityId<shared::OverworldTag>(game);
    if (lightId != kInvalidEntityId)
      hardcoded_spinning_light(game.registry, dt, lightId);
    scene_cycle_system(game.registry, StateType::OVERWORLD);
    return;
  }

  if (maze_puzzle::isPuzzleActive(game)) {
    maze_puzzle::updatePuzzle(game, dt);
    render_model_change(game, dt);

    uint32_t lightId = findLightEntityId<shared::OverworldTag>(game);
    if (lightId != kInvalidEntityId)
      hardcoded_spinning_light(game.registry, dt, lightId);
    scene_cycle_system(game.registry, StateType::OVERWORLD);
    return;
  }

  if (tangram_trigger::canTriggerTangram(game)) {
    const bool allInTypingTrigger =
        tangram_trigger::allActivePlayersInTangramTrigger(game);
    if (!allInTypingTrigger) {
      game.overworldTangramTriggerArmed = true;
      game.overworldTangramFocusTimer = 0.0f;
    } else if (game.overworldTangramTriggerArmed) {
      tangram_camera::snapOverworldAvatarsFaceTangramBoard(game);
      game.overworldTangramFocusTimer += dt;
      if (game.overworldTangramFocusTimer >= tangram_camera::kFocusHoldSeconds) {
        game.overworldTangramTriggerArmed = false;
        game.overworldTangramFocusTimer = 0.0f;
        tangram_puzzle::beginPuzzle(game);
        return;
      }
    }
  }
  tickOverworldGameLogic(game, dt);
  if (tangram_trigger::canTriggerTangram(game)) {
    const bool allInTypingTrigger =
        tangram_trigger::allActivePlayersInTangramTrigger(game);
    if (!allInTypingTrigger) {
      game.overworldTangramTriggerArmed = true;
      game.overworldTangramFocusTimer = 0.0f;
    } else if (game.overworldTangramTriggerArmed) {
      tangram_camera::snapOverworldAvatarsFaceTangramBoard(game);
      game.overworldTangramFocusTimer += dt;
      if (game.overworldTangramFocusTimer >= tangram_camera::kFocusHoldSeconds) {
        game.overworldTangramTriggerArmed = false;
        game.overworldTangramFocusTimer = 0.0f;
        tangram_puzzle::beginPuzzle(game);
        return;
      }
    }
  }

  const bool allInTrigger = maze_trigger::allActivePlayersInMazeTrigger(game);
  if (!allInTrigger) {
    game.overworldMazeTriggerArmed = true;
    game.overworldMazeFocusTimer = 0.0f;
  } else if (game.overworldMazeTriggerArmed &&
             maze_trigger::canTriggerMaze(game)) {
    maze_camera::snapOverworldAvatarsFaceMazePreview(game);
    game.overworldMazeFocusTimer += dt;
    if (game.overworldMazeFocusTimer >= maze_camera::kFocusHoldSeconds) {
      game.overworldMazeTriggerArmed = false;
      game.overworldMazeFocusTimer = 0.0f;
      maze_puzzle::beginPuzzle(game);
      return;
    }

    // Hijack KEY_CYCLE_SCENE for testing, e.g. cycling scenes different sounds
    // if (input.keys_newly_pressed & KEY_CYCLE_SCENE) {
    //     shared::SoundEventPacket pkt;
    //     pkt.soundId = static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED);
    //     pkt.volume = 1.0f;
    //     pkt.positional = false;
    //     net::broadcastPacket(game.network->getHost(), pkt);
    // }
  }
  render_model_change(game, dt);

  auto inputView = game.registry.view<shared::PlayerInput>();

  // DEBUG: press B to complete section 0
  for (auto ent : inputView) {
    auto& input = game.registry.get<shared::PlayerInput>(ent);
    if (input.keys_newly_pressed & KEY_DEBUG_COMPLETE_SECTION) {
      auto barrierView2 =
          game.registry.view<shared::SectionBarrierTag, shared::OverworldTag>();
      for (auto barrier : barrierView2) {
        auto& phys = game.registry.get<shared::PhysicsBody>(barrier);
        JPH::BodyID bodyId(phys.bodyId);
        auto& bodyInterface = game.physics.getBodyInterface();
        if (bodyInterface.IsAdded(bodyId)) {
          bodyInterface.RemoveBody(bodyId);
          printf("DEBUG: removed collision body\n");
        } else {
          bodyInterface.AddBody(bodyId, JPH::EActivation::DontActivate);
          printf("DEBUG: re-added collision body\n");
        }
      }
    }
  }

  // DEBUG: press N to toggle barrier visibility
  for (auto ent : inputView) {
    auto& input = game.registry.get<shared::PlayerInput>(ent);
    if (input.keys_newly_pressed & KEY_DEBUG_TOGGLE_BARRIERS) {
      auto barrierView =
          game.registry.view<shared::SectionBarrierTag, shared::OverworldTag>();
      printf("DEBUG: toggling %zu barriers\n", barrierView.size_hint());
      for (auto barrier : barrierView) {
        auto& tag = barrierView.get<shared::SectionBarrierTag>(barrier);
        uint32_t eid = game.registry.get<shared::Entity>(barrier).id;

        if (game.registry.all_of<shared::SectionBarrierVisible>(barrier)) {
          // hide: remove RenderInfo, tell client to despawn+respawn without it
          game.registry.remove<shared::RenderInfo>(barrier);
          game.registry.remove<shared::SectionBarrierVisible>(barrier);
        } else {
          // show: add RenderInfo back
          game.registry.emplace<shared::RenderInfo>(
              barrier, "cube", tag.halfExtents.x * 2.0f,
              tag.halfExtents.y * 2.0f, tag.halfExtents.z * 2.0f);
          game.registry.emplace<shared::SectionBarrierVisible>(barrier);
        }

        // despawn then respawn so client gets fresh component list
        shared::DespawnPacket despawn;
        despawn.type = shared::PacketType::DESPAWN_ENTITY;
        despawn.entityId = eid;
        net::broadcastPacket(game.network->getHost(), despawn);

        std::vector<entt::entity> toRespawn = {barrier};
        auto buf = serializeEntities(game.registry, game.componentRegistry,
                                     shared::PacketType::SPAWN_ENTITY,
                                     toRespawn, false);
        net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
      }
      break;
    }
  }

  uint32_t lightId = findLightEntityId<shared::OverworldTag>(game);
  if (lightId != kInvalidEntityId)
    hardcoded_spinning_light(game.registry, dt, lightId);
  scene_cycle_system(game.registry, StateType::OVERWORLD);
}

// ── MazeState ────────────────────────────────────────────

void MazeState::onEnter(ServerGame& game) {
  shared::StateChangePacket pkt;
  pkt.state = shared::GameStateType::MAZE;
  net::broadcastPacket(game.network->getHost(), pkt);

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
  game.overworldMazeFocusTimer = 0.0f;
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
