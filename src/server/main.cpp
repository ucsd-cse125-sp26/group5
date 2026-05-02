#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include "map_loader.h"
#include "scene.h"
#include "server_game.h"
#include "server_level_loader.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/hello.h"
#include "shared/input.h"
#include "shared/lighting.h"
#include "shared/map_format.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"
#include "shared/simple_profiler.h"
#include "shared/util.h"

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

  network.onConnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("A new client connected from %x:%u.\n", peer->address.host,
           peer->address.port);

    std::vector<entt::entity> existing;
    auto view = g.registry.view<shared::Entity>();
    for (auto ent : view) existing.push_back(ent);
    if (!existing.empty()) {
      auto buf =
          serializeEntities(g.registry, g.componentRegistry,
                            shared::PacketType::SPAWN_ENTITY, existing, false);
      net::sendRaw(peer, buf.data(), buf.size());
    }

    peer->data = (void*)"Client information";
    auto [entity_id, entity] = new_entity(g);
    g.peerEntityMap[peer] = entity;
    g.registry.emplace<shared::Position>(entity, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f);
    g.registry.emplace<shared::Velocity>(entity, 10.0f, 10.0f);
    g.registry.emplace<shared::RenderInfo>(entity, "cube", 1.0f, 1.0f, 1.0f);
    g.registry.emplace<shared::Camera>(entity, 0.0f, 1.0f);
    g.registry.emplace<shared::PlayerInput>(
        entity, static_cast<InputKeys>(0), static_cast<InputKeys>(0),
        static_cast<InputKeys>(0), 0.0f, 0.0f);
    JPH::BodyID bodyId = g.physics.createPlayerBody(
        "cube", glm::vec3(0.0f, 0.0f, 5.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f));
    g.registry.emplace<shared::PhysicsBody>(entity,
                                            bodyId.GetIndexAndSequenceNumber());

    auto buf =
        serializeEntities(g.registry, g.componentRegistry,
                          shared::PacketType::SPAWN_ENTITY, {entity}, false);
    net::broadcastRaw(network.getHost(), buf.data(), buf.size());

    shared::AssignPacket assignPkt;
    assignPkt.type = shared::PacketType::ASSIGN_ENTITY;
    assignPkt.entityId = entity_id;
    net::sendPacket(peer, assignPkt);
  };

  network.onDisconnect = [&network](ServerGame& g, ENetPeer* peer) {
    printf("%s disconnected.\n", static_cast<const char*>(peer->data));
    auto entity = g.peerEntityMap[peer];

    shared::DespawnPacket despawnPkt;
    despawnPkt.type = shared::PacketType::DESPAWN_ENTITY;
    despawnPkt.entityId = g.registry.get<shared::Entity>(entity).id;
    net::broadcastPacket(network.getHost(), despawnPkt);

    g.registry.destroy(entity);  // on_destroy<PhysicsBody> removes the body
    g.peerEntityMap.erase(peer);
    peer->data = nullptr;
  };

  registerServerHandlers(network);
  loadMap(game, (exeDir() / shared::DEFAULT_MAP_PATH).string());
  loadLevel(game);

  spawnStaticEntities(
      game, {
                // 100³ floor cube; top surface lands on z=0.
                StaticEntityDesc{.position = glm::vec3(0.0f, 0.0f, -50.0f),
                                 .modelName = "cube",
                                 .scale = glm::vec3(100.0f)},
                StaticEntityDesc{.position = glm::vec3(5.0f, 5.0f, 0.5f),
                                 .modelName = "cube",
                                 .scale = glm::vec3(1.0f)},
                StaticEntityDesc{.position = glm::vec3(10.0f, 0.0f, 0.0f),
                                 .modelName = "bear",
                                 .scale = glm::vec3(0.5f),
                                 .collision = CollisionShape::Box},
                StaticEntityDesc{.position = glm::vec3(20.0f, 0.0f, 0.0f),
                                 .modelName = "bear",
                                 .scale = glm::vec3(0.5f),
                                 .collision = CollisionShape::Mesh},
            });

  auto [light_entity_id, light_entity] = new_entity(game);
  game.registry.emplace<shared::Position>(light_entity, 5.0f, 0.0f, 3.0f, 1.0f,
                                          0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(light_entity, "light_cube", 0.2f,
                                            0.2f, 0.2f);
  constexpr auto kAtt = shared::kDefaultPointLightAttenuation;
  game.registry.emplace<shared::PointLight>(
      light_entity, 5.0f, 0.0f, 3.0f, kAtt.constant, kAtt.linear,
      kAtt.quadratic, 0.1f, 0.1f, 0.1f, 0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f);
  game.registry.emplace<shared::Scene>(light_entity, "sunny");

  auto previousTime = std::chrono::high_resolution_clock::now();
  const float fixedDt = 1.0f / 60.0f;
  float accumulator = 0.0f;

  while (true) {
    network.poll(game);

    auto currentTime = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(currentTime - previousTime).count();
    previousTime = currentTime;
    accumulator += dt;
    while (accumulator >= fixedDt) {
      input_tick(game.registry);
      movement_system(game, fixedDt);
      render_model_change(game, fixedDt);
      hardcoded_spinning_light(game.registry, fixedDt, light_entity_id);

      game.physics.step(fixedDt);

      // Jolt → ECS sync. Skip rotation for player entities; their yaw is
      // movement_system's responsibility (Jolt rotation DOFs are locked).
      auto& bi = game.physics.getBodyInterface();
      auto physicsView =
          game.registry.view<shared::Position, shared::PhysicsBody>();
      for (auto ent : physicsView) {
        auto& pos = physicsView.get<shared::Position>(ent);
        auto& pb = physicsView.get<shared::PhysicsBody>(ent);
        JPH::BodyID id(pb.bodyId);
        JPH::RVec3 jp = bi.GetPosition(id);
        pos.x = jp.GetX();
        pos.y = jp.GetY();
        pos.z = jp.GetZ();
        if (!game.registry.all_of<shared::PlayerInput>(ent)) {
          JPH::Quat jr = bi.GetRotation(id);
          pos.qw = jr.GetW();
          pos.qx = jr.GetX();
          pos.qy = jr.GetY();
          pos.qz = jr.GetZ();
        }
      }
      scene_cycle_system(game.registry);
      accumulator -= fixedDt;

      SIMPLE_PROFILE_SCOPE("Broadcast State");
      // dirtyOnly=false: full snapshot every tick for now.
      std::vector<entt::entity> allEnts;
      auto view = game.registry.view<shared::Entity>();
      for (auto ent : view) allEnts.push_back(ent);
      auto buf =
          serializeEntities(game.registry, game.componentRegistry,
                            shared::PacketType::UPDATE_ENTITY, allEnts, false);
      net::broadcastRaw(network.getHost(), buf.data(), buf.size());
      SIMPLE_PROFILE_FRAME_END("Server");
      SIMPLE_PROFILE_FRAME_START();
    }

    // Yield so the loop doesn't spin-lock at 100% CPU.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  network.shutdown();
  return EXIT_SUCCESS;
}
