#include "scene.h"

#include "physics_engine.h"
#include "shared/components.h"

void spawnStaticEntities(ServerGame& game,
                         const std::vector<StaticEntityDesc>& descs) {
  for (auto& desc : descs) {
    auto [id, entity] = new_entity(game);
    game.registry.emplace<shared::Position>(entity, desc.x, desc.y, desc.z,
                                            1.0f, 0.0f, 0.0f, 0.0f);
    if (desc.render) {
      game.registry.emplace<shared::RenderInfo>(entity, desc.modelName,
                                                desc.scale);
    }
    JPH::BodyID bodyId;
    if (desc.meshPath.empty()) {
      auto& bi = game.physics.getBodyInterface();
      float hx = desc.halfX > 0 ? desc.halfX : desc.scale * 0.5f;
      float hy = desc.halfY > 0 ? desc.halfY : desc.scale * 0.5f;
      float hz = desc.halfZ > 0 ? desc.halfZ : desc.scale * 0.5f;
      JPH::BoxShapeSettings box(JPH::Vec3(hx, hy, hz));
      box.SetEmbedded();
      JPH::ShapeRefC shape = box.Create().Get();
      printf("Box halfX=%.3f halfY=%.3f halfZ=%.3f\n", hx, hy, hz);
      JPH::BodyCreationSettings settings(
          shape, JPH::RVec3(desc.x, desc.y, desc.z), JPH::Quat::sIdentity(),
          JPH::EMotionType::Static, Layers::NON_MOVING);
      JPH::Body* body = bi.CreateBody(settings);
      bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
      bodyId = body->GetID();
    } else {
      bodyId = game.physics.createMeshBody(desc.meshPath, desc.x, desc.y,
                                           desc.z, desc.scale);
    }

    game.registry.emplace<shared::PhysicsBody>(
        entity, bodyId.GetIndexAndSequenceNumber());
  }
}