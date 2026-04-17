#include "server_game.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "glm/gtc/constants.hpp"
#include "glm/gtc/quaternion.hpp"
#include "server_network.h"
#include "shared/components.h"

// ── Movement system ──────────────────────────────────────

void movement_system(entt::registry& registry, float dt) {
  const float sensitivity = 0.002f;
  const float pitchLimit = glm::half_pi<float>() - 0.01f;

  auto view =
      registry.view<shared::Position, shared::Velocity, shared::PlayerInput>();
  for (auto entity : view) {
    auto& position = view.get<shared::Position>(entity);
    auto& velocity = view.get<shared::Velocity>(entity);
    auto& input = view.get<shared::PlayerInput>(entity);

    // Apply mouse look: yaw from mouseDx, pitch from mouseDy
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

    if (registry.all_of<shared::Camera>(entity)) {
      auto& cam = registry.get<shared::Camera>(entity);
      cam.pitch = std::clamp(cam.pitch - input.mouseDy * sensitivity,
                             -pitchLimit, pitchLimit);
    }

    // Clear mouse deltas after processing
    input.mouseDx = 0.0f;
    input.mouseDy = 0.0f;

    float yaw = std::atan2(
        2.0f * (position.qw * position.qz + position.qx * position.qy),
        1.0f - 2.0f * (position.qy * position.qy + position.qz * position.qz));
    float cy = std::cos(yaw);
    float sy = std::sin(yaw);
    // At yaw=0, forward=+y, right=+x (matches the old axis convention).
    float fwdX = -sy, fwdY = cy;
    float rightX = cy, rightY = sy;

    float fwdInput = 0.0f, strafeInput = 0.0f;
    if (input.keys & 0x01) fwdInput += 1.0f;     // W
    if (input.keys & 0x04) fwdInput -= 1.0f;     // S
    if (input.keys & 0x08) strafeInput += 1.0f;  // D
    if (input.keys & 0x02) strafeInput -= 1.0f;  // A

    const float speed = 10.0f;
    velocity.dx = (fwdInput * fwdX + strafeInput * rightX) * speed;
    velocity.dy = (fwdInput * fwdY + strafeInput * rightY) * speed;

    if (input.keys & 0x10)
      velocity.dz = 10.0f;
    else if (position.z > 0)
      velocity.dz = -10.0f;

    position.x += velocity.dx * dt;
    position.y += velocity.dy * dt;
    position.z += velocity.dz * dt;
    position.z = fmax(position.z, 0);
  }
}

// Model modification system
void render_model_change(entt::registry& registry, float dt) {
  auto view = registry.view<shared::RenderInfo, shared::PlayerInput>();
  for (auto entity : view) {
    auto& renderInfo = view.get<shared::RenderInfo>(entity);
    auto& input = view.get<shared::PlayerInput>(entity);
    if (input.keys & 0x80) {
      renderInfo.modelName = renderInfo.modelName == "cube" ? "bear" : "cube";
    }
    if (input.keys & 0x20) renderInfo.scale *= 1.1;
    if (input.keys & 0x40) renderInfo.scale /= 1.1;
  }
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

