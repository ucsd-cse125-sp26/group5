#pragma once
#include <enet/enet.h>

#include <entt/entt.hpp>
#include <map>
#include <vector>

#include "physics_engine.h"
#include "shared/component_registry.h"
#include "shared/protocol.h"
#include "game_state.h"
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

struct ServerGame {
  shared::ComponentRegistry componentRegistry;
  entt::registry registry;
  std::map<ENetPeer*, PlayerAvatars> active_players;
  std::vector<PlayerAvatars> unused_player_slots;
  uint32_t nextEntityId = 0;
  GameStateManager gameStateManager;
  ServerNetwork* network = nullptr;
  PhysicsEngine physics;
};

void input_tick(entt::registry& registry);
void movement_system(ServerGame& game, float dt, StateType stateType);
void render_model_change(entt::registry& registry, float dt);
void hardcoded_spinning_light(entt::registry& registry, float dt,
                              uint32_t lightEntity);
void scene_cycle_system(entt::registry& registry);
std::tuple<uint32_t, entt::entity> new_entity(ServerGame& g);
void registerServerHandlers(ServerNetwork& network);
void initWorldEntities(ServerGame& game);

std::vector<uint8_t> serializeEntities(
    entt::registry& registry,
    const shared::ComponentRegistry& componentRegistry,
    shared::PacketType packetType, const std::vector<entt::entity>& entities,
    bool dirtyOnly);