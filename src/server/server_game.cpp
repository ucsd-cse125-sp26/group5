#include "server_game.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "entt/entity/fwd.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/quaternion.hpp"
#include "server_network.h"
#include "shared/assets.h"
#include "shared/components.h"

constexpr float kHeldKeyScaleFactor = 1.1f;

// ── Movement system ──────────────────────────────────────

// Process input on tick
void input_tick(entt::registry& registry) {
  auto view = registry.view<shared::PlayerInput>();
  for (auto entity : view) {
    auto& playerInput = view.get<shared::PlayerInput>(entity);
    playerInput.keys_newly_pressed = ~playerInput.keys_prev & playerInput.keys;
    playerInput.keys_prev = playerInput.keys;
  }
}

// ── Movement system ──────────────────────────────────────

void movement_system(ServerGame& game, float dt) {
  const float sensitivity = 0.002f;
  const float pitchLimit = glm::half_pi<float>() - 0.01f;
  auto& bodyInterface = game.physics.getBodyInterface();

  auto view = game.registry.view<shared::Position, shared::Velocity,
                                 shared::PlayerInput, shared::PhysicsBody>();
  for (auto entity : view) {
    auto& position = view.get<shared::Position>(entity);
    auto& velocity = view.get<shared::Velocity>(entity);
    auto& input = view.get<shared::PlayerInput>(entity);
    auto& pb = view.get<shared::PhysicsBody>(entity);
    JPH::BodyID bodyId(pb.bodyId);

    // Apply mouse look
    if (input.mouseDx != 0.0f) {
      float yawDelta = -input.mouseDx * sensitivity;
      glm::quat q(position.qw, position.qx, position.qy, position.qz);
      glm::quat yawQ = glm::angleAxis(yawDelta, glm::vec3(0, 0, 1));
      q = glm::normalize(yawQ * q);
      position.qw = q.w;
      position.qx = q.x;
      position.qy = q.y;
      position.qz = q.z;
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

    const float speed = 10.0f;
    velocity.dx = (fwdInput * fwdX + strafeInput * rightX) * speed;
    velocity.dy = (fwdInput * fwdY + strafeInput * rightY) * speed;

    // Get current vertical velocity from Jolt to preserve gravity
    JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(bodyId);
    float verticalVel = currentVel.GetZ();

    // Jump
    if (input.keys_newly_pressed & KEY_JUMP) verticalVel = 10.0f;

    // Set velocity on Jolt body instead of manually moving position
    bodyInterface.SetLinearVelocity(
        bodyId, JPH::Vec3(velocity.dx, velocity.dy, verticalVel));
  }
}

// Model modification system
void render_model_change(entt::registry& registry, float dt) {
  auto view = registry.view<shared::RenderInfo, shared::PlayerInput>();
  for (auto entity : view) {
    auto& renderInfo = view.get<shared::RenderInfo>(entity);
    auto& input = view.get<shared::PlayerInput>(entity);
    if (input.keys_newly_pressed & KEY_SWAP_MODEL) {
      renderInfo.modelName = renderInfo.modelName == "cube" ? "bear" : "cube";
    }
    if (input.keys & KEY_MODEL_BIGGER) renderInfo.scale *= 1.1;
    if (input.keys & KEY_MODEL_SMALLER) renderInfo.scale /= 1.1;
  }
}

// Temporary - used to demonstrate server-controlled point light
void hardcoded_spinning_light(entt::registry& registry, float dt,
                              uint32_t light_entity_id) {
  bool brighten = false;
  bool dim = false;

  auto input_view = registry.view<shared::PlayerInput>();
  for (auto entity : input_view) {
    auto& input = input_view.get<shared::PlayerInput>(entity);
    if (input.keys & KEY_LIGHT_BRIGHT) brighten = true;
    if (input.keys & KEY_LIGHT_DIM) dim = true;
  }

  static float angle = 0.0f;
  angle += dt * 1.0f;  // 1 radian/sec

  const float radius = 5.0f;
  const float height = 3.0f;

  auto view =
      registry.view<shared::Position, shared::PointLight, shared::Entity>();
  for (auto entity : view) {
    auto& eid = view.get<shared::Entity>(entity);
    if (eid.id != light_entity_id) continue;

    auto& pos = view.get<shared::Position>(entity);
    auto& light = view.get<shared::PointLight>(entity);

    pos.x = radius * std::cos(angle);
    pos.y = radius * std::sin(angle);
    pos.z = height;

    // Orient the cube to face the origin
    glm::vec3 p(pos.x, pos.y, pos.z);
    glm::vec3 dir = glm::normalize(-p);
    glm::quat q = glm::quatLookAt(dir, glm::vec3(0.0f, 0.0f, 1.0f));
    pos.qw = q.w;
    pos.qx = q.x;
    pos.qy = q.y;
    pos.qz = q.z;

    light.px = pos.x;
    light.py = pos.y;
    light.pz = pos.z;

    if (brighten) {
      light.diffuseR *= kHeldKeyScaleFactor;
      light.diffuseG *= kHeldKeyScaleFactor;
      light.diffuseB *= kHeldKeyScaleFactor;
      light.specularR *= kHeldKeyScaleFactor;
      light.specularG *= kHeldKeyScaleFactor;
      light.specularB *= kHeldKeyScaleFactor;
    }
    if (dim) {
      light.diffuseR /= kHeldKeyScaleFactor;
      light.diffuseG /= kHeldKeyScaleFactor;
      light.diffuseB /= kHeldKeyScaleFactor;
      light.specularR /= kHeldKeyScaleFactor;
      light.specularG /= kHeldKeyScaleFactor;
      light.specularB /= kHeldKeyScaleFactor;
    }
  }
}

void scene_cycle_system(entt::registry& registry) {
  bool cycle = false;
  auto inputView = registry.view<shared::PlayerInput>();
  for (auto entity : inputView) {
    auto& input = inputView.get<shared::PlayerInput>(entity);
    if (input.keys_newly_pressed & KEY_CYCLE_SCENE) {
      cycle = true;
      break;
    }
  }
  if (!cycle) return;

  auto sceneView = registry.view<shared::Scene>();
  for (auto entity : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(entity);
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

// Entity creation helper
std::tuple<uint32_t, entt::entity> new_entity(ServerGame& g) {
  auto entity = g.registry.create();
  auto id = g.nextEntityId;
  g.registry.emplace<shared::Entity>(entity, id);
  g.nextEntityId++;
  return {id, entity};
}

// ── Packet handlers ──────────────────────────────────────

void registerServerHandlers(ServerNetwork& network) {
  network.dispatcher().on(
      shared::PacketType::INPUT,
      [](ServerGame& game, ENetPeer* sender, const uint8_t* data, size_t len) {
        shared::InputPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        auto it = game.peerEntityMap.find(sender);
        if (it == game.peerEntityMap.end()) return;
        auto ent = it->second;

        auto& playerInput = game.registry.get<shared::PlayerInput>(ent);
        playerInput.keys = pkt.keys;
        playerInput.mouseDx += pkt.mouseDx;
        playerInput.mouseDy += pkt.mouseDy;
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

JPH::BodyID createPlayerBody(ServerGame& game, float x, float y, float z) {
  auto& bodyInterface = game.physics.getBodyInterface();

  JPH::CapsuleShapeSettings capsuleSettings(0.5f, 0.5f);
  JPH::ShapeSettings::ShapeResult capsuleResult = capsuleSettings.Create();
  JPH::ShapeRefC shape = capsuleResult.Get();

  JPH::BodyCreationSettings settings(shape, JPH::RVec3(x, y, z),
                                     JPH::Quat::sIdentity(),
                                     JPH::EMotionType::Dynamic, Layers::MOVING);
  settings.mGravityFactor = 1.0f;
  settings.mFriction = 0.5f;

  settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                          JPH::EAllowedDOFs::TranslationY |
                          JPH::EAllowedDOFs::TranslationZ;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

  JPH::Body* body = bodyInterface.CreateBody(settings);
  bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
  return body->GetID();
}

JPH::BodyID createFloor(ServerGame& game) {
  auto& bodyInterface = game.physics.getBodyInterface();
  JPH::BoxShapeSettings floorShapeSettings(JPH::Vec3(100.0f, 100.0f, 1.0f));
  floorShapeSettings.SetEmbedded();
  JPH::ShapeSettings::ShapeResult floorShapeResult =
      floorShapeSettings.Create();
  JPH::ShapeRefC floorShape = floorShapeResult.Get();
  JPH::BodyCreationSettings floorSettings(
      floorShape, JPH::RVec3(0.0f, 0.0f, -1.0f), JPH::Quat::sIdentity(),
      JPH::EMotionType::Static, Layers::NON_MOVING);
  JPH::Body* floor = bodyInterface.CreateBody(floorSettings);
  bodyInterface.AddBody(floor->GetID(), JPH::EActivation::DontActivate);
  return floor->GetID();
}

JPH::BodyID createMeshBody(ServerGame& game, const std::string& filename,
                           float x, float y, float z, float scale) {
  auto& bodyInterface = game.physics.getBodyInterface();

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
      filename, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

  if (!scene || !scene->mRootNode) {
    printf("Failed to load mesh for physics: %s\n", filename.c_str());
    // fallback to small box
    JPH::BoxShapeSettings fallback(JPH::Vec3(0.5f, 0.5f, 0.5f));
    fallback.SetEmbedded();
    JPH::ShapeRefC shape = fallback.Create().Get();
    JPH::BodyCreationSettings settings(
        shape, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    JPH::Body* body = bodyInterface.CreateBody(settings);
    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID();
  }

  // Compute bounding box from all meshes
  float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
  float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    const aiMesh* mesh = scene->mMeshes[m];
    for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
      const aiVector3D& vert = mesh->mVertices[v];
      minX = std::min(minX, vert.x);
      minY = std::min(minY, vert.y);
      minZ = std::min(minZ, vert.z);
      maxX = std::max(maxX, vert.x);
      maxY = std::max(maxY, vert.y);
      maxZ = std::max(maxZ, vert.z);
    }
  }

  float halfX = (maxX - minX) / 2.0f * scale;
  float halfY = (maxZ - minZ) / 2.0f * scale;
  float halfZ = (maxY - minY) / 2.0f * scale;

  float centerX = x;
  float centerY = y;
  float centerZ = z;

  JPH::BoxShapeSettings boxSettings(JPH::Vec3(halfX, halfY, halfZ));
  boxSettings.SetEmbedded();
  JPH::ShapeRefC shape = boxSettings.Create().Get();
  JPH::BodyCreationSettings settings(
      shape, JPH::RVec3(centerX, centerY, centerZ), JPH::Quat::sIdentity(),
      JPH::EMotionType::Static, Layers::NON_MOVING);

  JPH::Body* body = bodyInterface.CreateBody(settings);
  bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
  return body->GetID();
}