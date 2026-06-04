#include "server_game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

#include "entt/entity/fwd.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/quaternion.hpp"
#include "server/game/puzzles/maze/layout_editor.h"
#include "server_network.h"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/net/packet_utils.h"
#include "shared/puzzles/maze/layout.h"
#include "shared/simple_profiler.h"
#include "shared/sound_constants.h"

namespace {

void onPhysicsBodyDestroyed(PhysicsEngine& physics, entt::registry& reg,
                            entt::entity ent) {
  physics.destroyBody(reg.get<shared::PhysicsBody>(ent).bodyId);
}

}  // namespace

void initServerGame(ServerGame& game) {
  game.registry.on_destroy<shared::PhysicsBody>()
      .connect<&onPhysicsBodyDestroyed>(game.physics);
}

static std::unordered_map<uint32_t, float> footstepTimers;
static std::mt19937 footstepRng(42);
// Process input on tick
void input_tick(entt::registry& registry) {
  SIMPLE_PROFILE_SCOPE("Input Tick");
  auto view = registry.view<shared::PlayerInput>();
  for (auto entity : view) {
    auto& playerInput = view.get<shared::PlayerInput>(entity);
    playerInput.keys_newly_pressed = ~playerInput.keys_prev & playerInput.keys;
    playerInput.keys_prev = playerInput.keys;
  }
}

// ── Movement system ──────────────────────────────────────

template <typename WorldTag>
static void movement_system_for_world(ServerGame& game, float dt) {
  SIMPLE_PROFILE_SCOPE("Movement System");
  const float sensitivity = 0.002f;
  const float pitchLimit = glm::half_pi<float>() - 0.01f;
  auto& bodyInterface = game.physics.getBodyInterface();

  auto view =
      game.registry.view<shared::Position, shared::Velocity,
                         shared::PlayerInput, shared::PhysicsBody, WorldTag>();
  for (auto entity : view) {
    auto& position = view.template get<shared::Position>(entity);
    auto& velocity = view.template get<shared::Velocity>(entity);
    auto& input = view.template get<shared::PlayerInput>(entity);
    auto& pb = view.template get<shared::PhysicsBody>(entity);
    JPH::BodyID bodyId(pb.bodyId);
    if (!bodyInterface.IsAdded(bodyId)) continue;

    if (input.mouseDx != 0.0f) {
      float yawDelta = -input.mouseDx * sensitivity;
      glm::quat q(position.qw, position.qx, position.qy, position.qz);
      glm::quat yawQ = glm::angleAxis(yawDelta, glm::vec3(0, 0, 1));
      q = glm::normalize(yawQ * q);
      position.qw = q.w;
      position.qx = q.x;
      position.qy = q.y;
      position.qz = q.z;

      // Push yaw so asymmetric shapes (bear box) track the facing.
      JPH::Quat joltRot(q.x, q.y, q.z, q.w);
      bodyInterface.SetRotation(bodyId, joltRot, JPH::EActivation::Activate);
    }

    if (game.registry.all_of<shared::Camera>(entity)) {
      auto& cam = game.registry.get<shared::Camera>(entity);
      cam.pitch = std::clamp(cam.pitch - input.mouseDy * sensitivity,
                             -pitchLimit, pitchLimit);
    }

    input.mouseDx = 0.0f;
    input.mouseDy = 0.0f;

    float yaw = std::atan2(
        2.0f * (position.qw * position.qz + position.qx * position.qy),
        1.0f - 2.0f * (position.qy * position.qy + position.qz * position.qz));
    float cy = std::cos(yaw);
    float sy = std::sin(yaw);
    float fwdX = -sy, fwdY = cy;
    float rightX = cy, rightY = sy;

    float fwdInput = 0.0f, strafeInput = 0.0f;
    if (input.keys & KEY_FORWARD) fwdInput += 1.0f;
    if (input.keys & KEY_BACKWARD) fwdInput -= 1.0f;
    if (input.keys & KEY_RIGHT) strafeInput += 1.0f;
    if (input.keys & KEY_LEFT) strafeInput -= 1.0f;

    const float speed = game.overworldTangramActive ? 4.0f : 10.0f;
    velocity.dx = (fwdInput * fwdX + strafeInput * rightX) * speed;
    velocity.dy = (fwdInput * fwdY + strafeInput * rightY) * speed;

    // Preserve gravity-driven Z velocity.
    JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(bodyId);
    float verticalVel = currentVel.GetZ();

    if (input.keys_newly_pressed & KEY_JUMP) {
      verticalVel = 10.0f;
      shared::SoundEventPacket pkt;
      pkt.soundId = static_cast<uint32_t>(shared::SoundId::JUMP);
      pkt.x = position.x;
      pkt.y = position.y;
      pkt.z = position.z;
      net::broadcastPacket(game.network->getHost(), pkt);
    }

    bool knocked =
        game.registry.all_of<shared::Knockback>(entity) &&
        game.registry.get<shared::Knockback>(entity).remaining > 0.0f;
    if (knocked) {
      // Physics owns X/Y; only enforce jump/gravity on Z.
      bodyInterface.SetLinearVelocity(
          bodyId, JPH::Vec3(currentVel.GetX(), currentVel.GetY(), verticalVel));
    } else {
      bodyInterface.SetLinearVelocity(
          bodyId, JPH::Vec3(velocity.dx, velocity.dy, verticalVel));
    }

    // ── Landing detection (ECS-driven via Grounded component) ──
    if (game.registry.all_of<shared::Grounded>(entity)) {
      auto& g = game.registry.get<shared::Grounded>(entity);
      if (!g.wasGrounded && g.isGrounded) {
        shared::SoundEventPacket pkt;
        pkt.soundId = static_cast<uint32_t>(shared::SoundId::LAND);
        pkt.x = position.x;
        pkt.y = position.y;
        pkt.z = position.z;
        pkt.volume = 0.7f;
        pkt.positional = true;
        net::broadcastPacket(game.network->getHost(), pkt);
      }
    }

    // ── Footsteps ──
    bool isMoving = (fwdInput != 0.0f || strafeInput != 0.0f);
    bool isGrounded = std::abs(verticalVel) < 0.5f;
    uint32_t eid = game.registry.get<shared::Entity>(entity).id;

    if (isMoving && isGrounded) {
      float& timer = footstepTimers[eid];
      timer += dt;
      const float footstepInterval = 0.4f;
      if (timer >= footstepInterval) {
        timer = 0.0f;
        std::uniform_int_distribution<int> variantDist(0, 3);
        uint32_t soundId = static_cast<uint32_t>(shared::SoundId::FOOTSTEP_1) +
                           variantDist(footstepRng);
        std::uniform_real_distribution<float> pitchDist(0.9f, 1.1f);
        shared::SoundEventPacket pkt;
        pkt.soundId = soundId;
        pkt.x = position.x;
        pkt.y = position.y;
        pkt.z = position.z;
        pkt.volume = 0.6f;
        pkt.pitch = pitchDist(footstepRng);
        pkt.positional = true;
        net::broadcastPacket(game.network->getHost(), pkt);
      }
    } else {
      footstepTimers[eid] = 0.0f;
    }
  }
}

void movement_system(ServerGame& game, float dt, StateType stateType) {
  switch (stateType) {
    case StateType::OVERWORLD:
      movement_system_for_world<shared::OverworldTag>(game, dt);
      break;
    case StateType::MAZE:
      movement_system_for_world<shared::MazeTag>(game, dt);
      break;
  }
}

// ── Held-key model resize ────────────────────────────────
//
// Resize is uncapped from input; clamp before sub-FLT_MIN scale crashes the
// Jolt narrow-phase or extreme scales wreck broadphase.
void render_model_change(ServerGame& game, float dt) {
  auto& bodyInterface = game.physics.getBodyInterface();
  auto view =
      game.registry
          .view<shared::RenderInfo, shared::PlayerInput, shared::PhysicsBody>();
  constexpr float kMinPlayerScale = 0.05f;
  constexpr float kMaxPlayerScale = 20.0f;
  for (auto entity : view) {
    auto& renderInfo = view.get<shared::RenderInfo>(entity);
    auto& input = view.get<shared::PlayerInput>(entity);
    auto& pb = view.get<shared::PhysicsBody>(entity);
    bool shapeDirty = false;
    if (input.keys_newly_pressed & KEY_SWAP_MODEL) {
      // Find the current model in the cycle; if it's not in the list (e.g.
      // a non-player model assigned for testing) fall back to index 0.
      size_t idx = shared::PLAYER_MODEL_CYCLE_COUNT;
      for (size_t i = 0; i < shared::PLAYER_MODEL_CYCLE_COUNT; ++i) {
        if (renderInfo.modelName == shared::PLAYER_MODEL_CYCLE[i]) {
          idx = i;
          break;
        }
      }
      size_t next = idx == shared::PLAYER_MODEL_CYCLE_COUNT
                        ? 0
                        : (idx + 1) % shared::PLAYER_MODEL_CYCLE_COUNT;
      renderInfo.modelName = std::string(shared::PLAYER_MODEL_CYCLE[next]);
      shapeDirty = true;
    }
    if (input.keys & KEY_MODEL_BIGGER) {
      renderInfo.sx = std::min(renderInfo.sx * 1.1f, kMaxPlayerScale);
      renderInfo.sy = std::min(renderInfo.sy * 1.1f, kMaxPlayerScale);
      renderInfo.sz = std::min(renderInfo.sz * 1.1f, kMaxPlayerScale);
      shapeDirty = true;
    }
    if (input.keys & KEY_MODEL_SMALLER) {
      renderInfo.sx = std::max(renderInfo.sx / 1.1f, kMinPlayerScale);
      renderInfo.sy = std::max(renderInfo.sy / 1.1f, kMinPlayerScale);
      renderInfo.sz = std::max(renderInfo.sz / 1.1f, kMinPlayerScale);
      shapeDirty = true;
    }
    if (shapeDirty) {
      const glm::vec3 newScale(renderInfo.sx, renderInfo.sy, renderInfo.sz);
      JPH::ShapeRefC newShape =
          game.physics.convexHullForAsset(renderInfo.modelName, newScale);
      if (!newShape)
        newShape =
            game.physics.playerShapeForAsset(renderInfo.modelName, newScale);
      // Don't recompute mass: a 14x11x18 bear box at default density is
      // ~2.7M kg, which combined with locked rotation DOFs produces
      // NaN/Inf in the next physics step.
      if (newShape) {
        bodyInterface.SetShape(JPH::BodyID(pb.bodyId), newShape,
                               /*update mass*/ false,
                               JPH::EActivation::Activate);
      }
    }
  }
}

// Cycle only the active world's Scene anchor. Iterating all Scene
// components would mutate an inactive anchor (Maze's, since EnTT iterates
// newest-first), and that change never reaches clients because UPDATE_ENTITY
// only broadcasts the active state's tagged entities.
template <typename Tag>
static void scene_cycle_system_for_world(entt::registry& registry) {
  bool cycle = false;
  auto inputView = registry.view<Tag, shared::PlayerInput>();
  for (auto entity : inputView) {
    auto& input = inputView.template get<shared::PlayerInput>(entity);
    if (input.keys_newly_pressed & KEY_CYCLE_SCENE) {
      cycle = true;
      break;
    }
  }
  if (!cycle) return;

  auto sceneView = registry.view<Tag, shared::Scene>();
  for (auto entity : sceneView) {
    auto& scene = sceneView.template get<shared::Scene>(entity);
    for (std::size_t i = 0; i < shared::SCENE_COUNT; i++) {
      if (shared::SCENES[i].name == scene.name) {
        scene.name =
            std::string(shared::SCENES[(i + 1) % shared::SCENE_COUNT].name);
        return;
      }
    }
    scene.name = std::string(shared::SCENES[0].name);
  }
}

void scene_cycle_system(entt::registry& registry, StateType stateType) {
  switch (stateType) {
    case StateType::OVERWORLD:
      scene_cycle_system_for_world<shared::OverworldTag>(registry);
      break;
    case StateType::MAZE:
      scene_cycle_system_for_world<shared::MazeTag>(registry);
      break;
  }
}

void update_grounded_system(ServerGame& game) {
  auto view = game.registry.view<shared::Grounded, shared::PhysicsBody>();
  for (auto entity : view) {
    auto& grounded = view.get<shared::Grounded>(entity);
    auto& pb = view.get<shared::PhysicsBody>(entity);
    JPH::BodyID bodyId(pb.bodyId);

    if (!game.physics.getBodyInterface().IsAdded(bodyId)) {
      // Body is inactive — keep both fields in sync so no edge fires on
      // re-activation
      grounded.wasGrounded = false;
      grounded.isGrounded = false;
      continue;
    }

    grounded.wasGrounded = grounded.isGrounded;
    grounded.isGrounded = game.physics.isBodyGrounded(bodyId);
  }
}

std::tuple<uint32_t, entt::entity> new_entity(ServerGame& g) {
  auto entity = g.registry.create();
  auto id = g.nextEntityId;
  g.registry.emplace<shared::Entity>(entity, id);
  g.nextEntityId++;
  return {id, entity};
}

void registerServerHandlers(ServerNetwork& network) {
  network.dispatcher().on(
      shared::PacketType::INPUT,
      [](ServerGame& game, ENetPeer* sender, const uint8_t* data, size_t len) {
        shared::InputPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        auto it = game.active_players.find(sender);
        if (it == game.active_players.end()) {
          std::printf("[Server] INPUT dropped: sender not in active_players\n");
          return;
        }

        entt::entity ent = entt::null;
        auto state = game.gameStateManager.currentState();
        if (state && state->getStateType() == StateType::OVERWORLD) {
          ent = it->second.overworld_avatar;
        } else if (state && state->getStateType() == StateType::MAZE) {
          ent = it->second.maze_avatar;
        }
        if (ent == entt::null) {
          std::printf(
              "[Server] INPUT dropped: no active avatar for current state\n");
          return;
        }

        auto& playerInput = game.registry.get<shared::PlayerInput>(ent);
        playerInput.keys = pkt.keys;
        playerInput.mouseDx += pkt.mouseDx;
        playerInput.mouseDy += pkt.mouseDy;
        playerInput.rotateTargetId = pkt.rotateTargetId;
      });

  // Demo debug control panel. Deferred: just record the command; the fixed-step
  // loop drains pendingDebugCommands via server_debug::processPendingCommands so
  // game logic runs on the game thread at a safe point.
  network.dispatcher().on(
      shared::PacketType::DEBUG_COMMAND,
      [](ServerGame& game, ENetPeer* sender, const uint8_t* data, size_t len) {
        (void)sender;
        if (len < sizeof(shared::DebugCommandPacket)) return;
        shared::DebugCommandPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        game.pendingDebugCommands.push_back(pkt);
      });
}

// ── Entity serialization ─────────────────────────────────
//
// Wire format (shared by SPAWN_ENTITY and UPDATE_ENTITY):
//
// ┌─────────────────────────────────────────────────────────┐
// │ PacketType (1 byte)  │  entityCount (2 bytes)           │
// ├─────────────────────────────────────────────────────────┤
// │ entityId (4 bytes)   │  componentCount (2 bytes)        │
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │
// │   └─ ...                                               │
// ├─────────────────────────────────────────────────────────┤
// │ ...more entities...                                     │
// └─────────────────────────────────────────────────────────┘

std::vector<uint8_t> serializeEntities(
    entt::registry& registry,
    const shared::ComponentRegistry& componentRegistry,
    shared::PacketType packetType, const std::vector<entt::entity>& entities,
    bool dirtyOnly) {
  std::vector<uint8_t> buffer;

  size_t headerPos = buffer.size();
  buffer.resize(buffer.size() + sizeof(shared::PacketType) + sizeof(uint16_t));

  uint16_t entityCount = 0;
  for (auto ent : entities) {
    if (!registry.valid(ent) || !registry.all_of<shared::Entity>(ent)) continue;

    uint32_t entityId = registry.get<shared::Entity>(ent).id;
    size_t entityHeaderPos = buffer.size();
    buffer.resize(buffer.size() + sizeof(uint32_t) + sizeof(uint16_t));

    uint16_t compCount = 0;
    for (auto cid : componentRegistry.syncedIds()) {
      (void)dirtyOnly;

      auto* meta = componentRegistry.find(cid);
      if (!meta) continue;
      size_t before = buffer.size();
      buffer.resize(buffer.size() + sizeof(uint16_t) + sizeof(uint16_t));
      std::vector<uint8_t> compBuf;
      if (meta->serialize(registry, ent, compBuf)) {
        auto dataSize = static_cast<uint16_t>(compBuf.size());
        std::memcpy(&buffer[before], &cid, sizeof(uint16_t));
        std::memcpy(&buffer[before + sizeof(uint16_t)], &dataSize,
                    sizeof(uint16_t));
        buffer.insert(buffer.end(), compBuf.begin(), compBuf.end());
        compCount++;
      } else {
        buffer.resize(before);
      }
    }

    std::memcpy(&buffer[entityHeaderPos], &entityId, sizeof(uint32_t));
    std::memcpy(&buffer[entityHeaderPos + sizeof(uint32_t)], &compCount,
                sizeof(uint16_t));
    entityCount++;
  }

  std::memcpy(&buffer[headerPos], &packetType, sizeof(shared::PacketType));
  std::memcpy(&buffer[headerPos + sizeof(shared::PacketType)], &entityCount,
              sizeof(uint16_t));

  return buffer;
}