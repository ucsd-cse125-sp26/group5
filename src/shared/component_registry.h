// shared/component_registry.h
#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <entt/entt.hpp>
#include <unordered_map>
#include "shared/components.h"

namespace shared {

using ComponentTypeId = uint16_t;

// Writes the component data for one entity into a byte buffer.
// Returns false if entity doesn't have this component.
using SerializeFn = std::function<bool(
    entt::registry& reg, entt::entity entity, std::vector<uint8_t>& buffer)>;

// Reads component data from a byte buffer and applies it to the entity.
// Returns how many bytes were consumed.
using DeserializeFn = std::function<size_t(
    entt::registry& reg, entt::entity entity, const uint8_t* data, size_t len)>;

struct ComponentMeta {
    SerializeFn   serialize;
    DeserializeFn deserialize;
};

inline std::unordered_map<ComponentTypeId, ComponentMeta>& getComponentRegistry() {
    static std::unordered_map<ComponentTypeId, ComponentMeta> registry;
    return registry;
}

inline std::vector<ComponentTypeId>& getSyncedComponentIds() {
    static std::vector<ComponentTypeId> ids;
    return ids;
}

template <typename T>
void registerComponent(ComponentTypeId id) {
    auto& reg = getComponentRegistry();
    auto& ids = getSyncedComponentIds();

    reg[id] = ComponentMeta{
        // serialize
        [](entt::registry& r, entt::entity e, std::vector<uint8_t>& buf) -> bool {
            if (!r.all_of<T>(e)) return false;
            const auto& comp = r.get<T>(e);
            const auto* raw = reinterpret_cast<const uint8_t*>(&comp);
            buf.insert(buf.end(), raw, raw + sizeof(T));
            return true;
        },
        // deserialize
        [](entt::registry& r, entt::entity e, const uint8_t* data, size_t len) -> size_t {
            if (len < sizeof(T)) return 0;
            T comp;
            std::memcpy(&comp, data, sizeof(T));
            r.emplace_or_replace<T>(e, comp);
            return sizeof(T);
        }
    };
    ids.push_back(id);
}

enum ComponentIds : ComponentTypeId {
    CID_POSITION = 1,
    CID_VELOCITY = 2,
    // add new IDs here as you add components — that's it
};
inline void registerAllSyncedComponents() {
    registerComponent<Position>(CID_POSITION);
    // registerComponent<Velocity>(CID_VELOCITY);  // uncomment to sync velocity too
    // registerComponent<Health>(CID_HEALTH);       // future components: one line
}


} // namespace shared