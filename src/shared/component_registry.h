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
        [](entt :: registry& src, entt::entity srcEnt,
          entt::registry& dst, entt::entity dstEnt) {
          if (!src.all_of<T>(srcEnt)) return;
          auto& srcComp = src.get<T>(srcEnt);
          dst.emplace_or_replace<T>(dstEnt, srcComp);
        }};
    syncedIds_.push_back(id);
  }

  const std::unordered_map<ComponentTypeId, ComponentMeta>& meta() const {
    return meta_;
  }

  const std::vector<ComponentTypeId>& syncedIds() const { return syncedIds_; }

  const ComponentMeta* find(ComponentTypeId id) const {
    auto it = meta_.find(id);
    return it != meta_.end() ? &it->second : nullptr;
  }

 private:
  std::unordered_map<ComponentTypeId, ComponentMeta> meta_;
  std::vector<ComponentTypeId> syncedIds_;
};

inline void cloneRegistry(const ComponentRegistry& compReg,
  entt::registry& src,
  const std::map<uint32_t, entt::entity>& srcMap,
  entt::registry& dst,
  std::map<uint32_t, entt::entity>& dstMap){
  
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
      meta->clone(src, srcEntity, dst, dstEntity);
    }
  }
}
enum ComponentIds : ComponentTypeId {
  CID_POSITION = 1,
  CID_ENTITY = 2,
  CID_RENDERINFO = 3,
  CID_CAMERA = 4,
};

inline ComponentRegistry createDefaultRegistry() {
  ComponentRegistry reg;
  reg.registerComponent<Position>(CID_POSITION);
  reg.registerComponent<Entity>(CID_ENTITY);
  reg.registerComponent<RenderInfo>(CID_RENDERINFO);
  reg.registerComponent<Camera>(CID_CAMERA);
  return reg;
}

}  // namespace shared
