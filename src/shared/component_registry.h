// shared/component_registry.h
#pragma once
#include <zpp_bits.h>

#include <cstdint>
#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>
#include <vector>

#include "shared/components.h"

namespace shared {

using ComponentTypeId = uint16_t;

using SerializeFn = std::function<bool(entt::registry& reg, entt::entity entity,
                                       std::vector<uint8_t>& buffer)>;

using DeserializeFn = std::function<size_t(
    entt::registry& reg, entt::entity entity, const uint8_t* data, size_t len)>;

using CloneFn = std::function<void(entt::registry& src, entt::entity srcEnt,
                                   entt::registry& dst, entt::entity dstEnt)>;
struct ComponentMeta {
  SerializeFn serialize;
  DeserializeFn deserialize;
  CloneFn clone;
  std::function<void(entt::registry&, entt::entity)> remove;
};

class ComponentRegistry {
 public:
  template <typename T>
  void registerComponent(ComponentTypeId id) {
    meta_[id] = ComponentMeta{
        [](entt::registry& r, entt::entity e,
           std::vector<uint8_t>& buf) -> bool {
          if (!r.all_of<T>(e)) return false;
          const auto& comp = r.get<T>(e);
          std::vector<std::byte> tmp;
          zpp::bits::out out(tmp);
          if (zpp::bits::failure(out(comp))) return false;
          const auto* raw = reinterpret_cast<const uint8_t*>(tmp.data());
          buf.insert(buf.end(), raw, raw + tmp.size());
          return true;
        },
        [](entt::registry& r, entt::entity e, const uint8_t* data,
           size_t len) -> size_t {
          T comp;
          auto span = std::span{reinterpret_cast<const std::byte*>(data), len};
          zpp::bits::in in(span);
          if (zpp::bits::failure(in(comp))) return 0;
          r.emplace_or_replace<T>(e, comp);
          return in.position();
        },
        [](entt::registry& src, entt::entity srcEnt, entt::registry& dst,
           entt::entity dstEnt) {
          if (!src.all_of<T>(srcEnt)) return;
          auto& srcComp = src.get<T>(srcEnt);
          dst.emplace_or_replace<T>(dstEnt, srcComp);
        },
        [](entt::registry& r, entt::entity e) { r.remove<T>(e); }};
    syncedIds_.push_back(id);
  }

  [[nodiscard]] const std::unordered_map<ComponentTypeId, ComponentMeta>& meta()
      const {
    return meta_;
  }

  [[nodiscard]] const std::vector<ComponentTypeId>& syncedIds() const {
    return syncedIds_;
  }

  [[nodiscard]] const ComponentMeta* find(ComponentTypeId id) const {
    auto it = meta_.find(id);
    return it != meta_.end() ? &it->second : nullptr;
  }

 private:
  std::unordered_map<ComponentTypeId, ComponentMeta> meta_;
  std::vector<ComponentTypeId> syncedIds_;
};

enum ComponentIds : ComponentTypeId {
  CID_POSITION = 1,
  CID_ENTITY = 2,
  CID_RENDERINFO = 3,
  CID_CAMERA = 4,
  CID_VELOCITY = 5,
  CID_POINTLIGHT = 6,
  CID_SCENE = 7,
  CID_DIRECTIONALLIGHT = 8,
  CID_OVERWORLD_MAZE_PUZZLE = 9,
  CID_COLORBOUNDINGBOX = 10,
  CID_ANIMATIONSTATE = 11,
  CID_MAZESPIRITGRID = 12,
  CID_SOUNDEMITTER = 13,
  CID_OVERWORLD_TANGRAM_PUZZLE = 14,
  CID_TANGRAM_PIECE = 15,
  CID_FALL_CHALLENGE = 16,
  CID_SUMMER_ESCAPE = 17,
  CID_SECTION_BARRIER = 18,
};

inline void cloneRegistry(const ComponentRegistry& compReg, entt::registry& src,
                          const std::map<uint32_t, entt::entity>& srcMap,
                          entt::registry& dst,
                          std::map<uint32_t, entt::entity>& dstMap) {
  auto removeSyncedComponent = [&](entt::entity entity, ComponentTypeId id) {
    switch (id) {
      case CID_POSITION:
        dst.remove<Position>(entity);
        break;
      case CID_ENTITY:
        dst.remove<Entity>(entity);
        break;
      case CID_RENDERINFO:
        dst.remove<RenderInfo>(entity);
        break;
      case CID_CAMERA:
        dst.remove<Camera>(entity);
        break;
      case CID_VELOCITY:
        dst.remove<Velocity>(entity);
        break;
      case CID_POINTLIGHT:
        dst.remove<PointLight>(entity);
        break;
      case CID_SCENE:
        dst.remove<Scene>(entity);
        break;
      case CID_DIRECTIONALLIGHT:
        dst.remove<DirectionalLight>(entity);
        break;
      case CID_OVERWORLD_MAZE_PUZZLE:
        dst.remove<OverworldMazePuzzleState>(entity);
        break;
      case CID_SOUNDEMITTER:
        dst.remove<SoundEmitter>(entity);
        break;
      case CID_COLORBOUNDINGBOX:
        dst.remove<ColorBoundingBox>(entity);
        break;
      case CID_ANIMATIONSTATE:
        dst.remove<AnimationState>(entity);
        break;
      case CID_MAZESPIRITGRID:
        dst.remove<MazeSpiritGrid>(entity);
        break;
      case CID_OVERWORLD_TANGRAM_PUZZLE:
        dst.remove<OverworldTangramPuzzleState>(entity);
        break;
      case CID_TANGRAM_PIECE:
        dst.remove<TangramPiece>(entity);
        break;
      case CID_FALL_CHALLENGE:
        dst.remove<FallChallengeState>(entity);
        break;
      case CID_SUMMER_ESCAPE:
        dst.remove<SummerEscapeState>(entity);
        break;
    }
  };

  // delete old entities in dst
  for (auto it = dstMap.begin(); it != dstMap.end();) {
    if (srcMap.find(it->first) == srcMap.end()) {
      dst.destroy(it->second);
      it = dstMap.erase(it);
    } else {
      it++;
    }
  }
  // create new entities in dst
  for (auto [entityId, srcEntity] : srcMap) {
    if (dstMap.find(entityId) != dstMap.end()) {
      continue;
    }
    auto dstEntity = dst.create();
    dstMap[entityId] = dstEntity;
  }
  // clone components from src to dst
  for (auto [entityId, srcEntity] : srcMap) {
    auto dstEntity = dstMap[entityId];
    for (auto id : compReg.syncedIds()) {
      auto meta = compReg.find(id);
      if (!meta) continue;
      if (id == CID_POSITION && !src.all_of<Position>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_ENTITY && !src.all_of<Entity>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_RENDERINFO && !src.all_of<RenderInfo>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_CAMERA && !src.all_of<Camera>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_VELOCITY && !src.all_of<Velocity>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_POINTLIGHT && !src.all_of<PointLight>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_SCENE && !src.all_of<Scene>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_DIRECTIONALLIGHT &&
          !src.all_of<DirectionalLight>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_OVERWORLD_MAZE_PUZZLE &&
          !src.all_of<OverworldMazePuzzleState>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_SOUNDEMITTER && !src.all_of<SoundEmitter>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_COLORBOUNDINGBOX &&
          !src.all_of<ColorBoundingBox>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_ANIMATIONSTATE && !src.all_of<AnimationState>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      if (id == CID_MAZESPIRITGRID && !src.all_of<MazeSpiritGrid>(srcEntity)) {
        removeSyncedComponent(dstEntity, id);
        continue;
      }
      meta->clone(src, srcEntity, dst, dstEntity);
    }
  }
}
inline ComponentRegistry createDefaultRegistry() {
  ComponentRegistry reg;
  reg.registerComponent<Position>(CID_POSITION);
  reg.registerComponent<Entity>(CID_ENTITY);
  reg.registerComponent<RenderInfo>(CID_RENDERINFO);
  reg.registerComponent<Camera>(CID_CAMERA);
  reg.registerComponent<PointLight>(CID_POINTLIGHT);
  reg.registerComponent<Scene>(CID_SCENE);
  reg.registerComponent<DirectionalLight>(CID_DIRECTIONALLIGHT);
  reg.registerComponent<SoundEmitter>(CID_SOUNDEMITTER);
  reg.registerComponent<OverworldMazePuzzleState>(CID_OVERWORLD_MAZE_PUZZLE);
  reg.registerComponent<OverworldTangramPuzzleState>(
      CID_OVERWORLD_TANGRAM_PUZZLE);
  reg.registerComponent<TangramPiece>(CID_TANGRAM_PIECE);
  reg.registerComponent<ColorBoundingBox>(CID_COLORBOUNDINGBOX);
  reg.registerComponent<AnimationState>(CID_ANIMATIONSTATE);
  // Replicated so the client can identify the maze spirit cube to attach
  // the first-person camera to (avoids fingerprinting by mesh+scale, which
  // collides with overworld decoration cubes that happen to share a scale).
  reg.registerComponent<MazeSpiritGrid>(CID_MAZESPIRITGRID);
  reg.registerComponent<FallChallengeState>(CID_FALL_CHALLENGE);
  reg.registerComponent<SummerEscapeState>(CID_SUMMER_ESCAPE);
  // Replicated so the client knows each active barrier's center (Position),
  // halfExtents and season — used to render the volumetric "barrier fog" wall.
  // Removal is by full entity destroy (despawn on unlock), which the dstMap
  // cleanup already handles, so no removeSyncedComponent case is needed.
  reg.registerComponent<SectionBarrierTag>(CID_SECTION_BARRIER);
  return reg;
}

}  // namespace shared
