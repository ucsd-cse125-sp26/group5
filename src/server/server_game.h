#pragma once
#include <enet/enet.h>

#include <array>
#include <cstdint>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <map>
#include <vector>

#include "game_state.h"
#include "physics_engine.h"
#include "shared/component_registry.h"
#include "shared/map_gamelogic_layout.h"
#include "shared/protocol.h"
#include "shared/puzzles/maze/layout.h"
#include "shared/puzzles/summer/layout.h"
#include "shared/puzzles/tangram/arena_layout.h"
#include "shared/puzzles/tangram/slot_layout.h"
class ServerNetwork;

struct PlayerAvatars {
  entt::entity overworld_avatar;
  entt::entity maze_avatar;

  void resetControls(entt::registry& registry) const {
    auto resetAvatar = [&registry](entt::entity avatar) {
      if (registry.all_of<shared::PlayerInput>(avatar)) {
        auto& input = registry.get<shared::PlayerInput>(avatar);
        input.keys = 0;
        input.keys_prev = 0;
        input.keys_newly_pressed = 0;
        input.mouseDx = 0.0f;
        input.mouseDy = 0.0f;
        input.rotateTargetId = 0;
      }
      if (registry.all_of<shared::Velocity>(avatar)) {
        auto& velocity = registry.get<shared::Velocity>(avatar);
        velocity.dx = 0.0f;
        velocity.dy = 0.0f;
        velocity.dz = 0.0f;
      }
    };
    resetAvatar(overworld_avatar);
    resetAvatar(maze_avatar);
  }
};

// `physics` first so it's destroyed last — the on_destroy<PhysicsBody> hook
// fires during ~registry and needs physics live.
struct ServerGame {
  PhysicsEngine physics;
  shared::ComponentRegistry componentRegistry;
  entt::registry registry;
  std::map<ENetPeer*, PlayerAvatars> active_players;
  std::vector<PlayerAvatars> unused_player_slots;
  uint32_t nextEntityId = 0;
  GameStateManager gameStateManager;
  ServerNetwork* network = nullptr;
  // Overworld maze trigger: when false, all players must leave the trigger
  // region once before another auto-enter (avoids instant re-entry after Q).
  bool overworldMazeTriggerArmed = true;
  // Accumulates while four players stand in the trigger facing the preview.
  float overworldMazeFocusTimer = 0.0f;

  // Overworld preview-board puzzle (no MazeState).
  bool overworldMazePuzzleActive = false;
  entt::entity overworldMazePuzzleController = entt::null;
  entt::entity overworldMazePieceEntity = entt::null;
  glm::vec3 overworldMazePreviewGoal{0.0f};
  float overworldMazePreviewBoardY = 0.0f;
  float overworldMazePreviewMinX = 0.0f;
  float overworldMazePreviewMaxX = 0.0f;
  float overworldMazePreviewMinZ = 0.0f;
  float overworldMazePreviewMaxZ = 0.0f;
  // Generated maze tiles for preview-board collision (0=wall, 1=floor).
  int overworldMazeGridWidth = 0;
  int overworldMazeGridHeight = 0;
  std::vector<uint8_t> overworldMazeGridTiles;
  float overworldMazeGridCenterX = 0.0f;
  float overworldMazeGridCenterZ = 0.0f;
  float overworldMazeGridXOffset = 0.0f;
  float overworldMazeGridYOffset = 0.0f;
  float overworldMazeLastValidX = 0.0f;
  float overworldMazeLastValidZ = 0.0f;
  int overworldMazeGoalTileX = 0;
  int overworldMazeGoalTileY = 0;
  bool overworldMazeReachGoalPending = false;

  // Set by KEY_DEBUG_CYCLE_SEASON (Y). When true, the per-tick
  // "force winter while winter unlocked+incomplete" clamp in MoveInMainMap is
  // skipped so the debug-cycled season actually sticks. One-way: stays on for
  // the rest of the run.
  bool debugSeasonOverride = false;

  shared::maze_layout::Config mazeLayout =
      shared::maze_layout::Config::defaults();
  shared::map_gamelogic_layout::FallLayout fallLayout{};

  // End-game: gather region around the "Fallen house" landmark. When all active
  // players are inside, clients roll the credits. `armed` re-arms once players
  // leave so re-entry can re-trigger (and avoids per-tick broadcast spam).
  shared::map_gamelogic_layout::FallenHouseRegion fallenHouseRegion{};
  bool creditsTriggerArmed = true;

  // Tangram puzzle (floating test platform in sky; legacy "fall board" naming).
  bool overworldTangramActive = false;
  bool overworldTangramTriggerArmed = true;
  float overworldTangramFocusTimer = 0.0f;
  entt::entity overworldTangramController = entt::null;
  std::array<entt::entity, 7> overworldFallFragmentEntities{};
  std::array<entt::entity, 7> overworldTangramGhostSlotEntities{};
  std::array<glm::vec2, 7> overworldFallFragmentSpawnXZ{};
  shared::tangram::ArenaLayout tangramArena =
      shared::tangram::ArenaLayout::defaults();
  shared::tangram_slot::Config tangramSlotLayout{};

  // Summer "escape" shrinking-zone minigame.
  shared::summer::Layout summerLayout = shared::summer::Layout::defaults();
  entt::entity summerEscapeController = entt::null;
  float summerWaveElapsed = 0.0f;  // seconds into the current wave
  float summerGrace = 0.0f;        // startup grace before "outside" can fail
  // Region (minX,minY,maxX,maxY) at the start of the current wave; the live
  // region interpolates from this toward the wave target.
  glm::vec4 summerWaveStartRegion{0.0f};
};

// Installs the on_destroy<PhysicsBody> hook. Call once. After it runs,
// destroy bodies by removing the entity, not via physics.destroyBody.
void initServerGame(ServerGame& game);

void input_tick(entt::registry& registry);
void movement_system(ServerGame& game, float dt, StateType stateType);
void render_model_change(ServerGame& game, float dt);
void scene_cycle_system(entt::registry& registry, StateType stateType);
void update_grounded_system(ServerGame& game);
std::tuple<uint32_t, entt::entity> new_entity(ServerGame& g);
void registerServerHandlers(ServerNetwork& network);
void initWorldEntities(ServerGame& game);

std::vector<uint8_t> serializeEntities(
    entt::registry& registry,
    const shared::ComponentRegistry& componentRegistry,
    shared::PacketType packetType, const std::vector<entt::entity>& entities,
    bool dirtyOnly);
