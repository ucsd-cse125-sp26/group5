#include "network.h"

// ┌─────────────────────────────────────────────────────────┐
// │ PacketType (1 byte)  │  entityCount (2 bytes)           │  ← "Packet header"
// ├─────────────────────────────────────────────────────────┤
// │ entityId (4 bytes)   │  componentCount (2 bytes)        │  ← Entity 0 header
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │  ← Component 0
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │  ← Component 1
// │   └─ ...                                               │
// ├─────────────────────────────────────────────────────────┤
// │ entityId (4 bytes)   │  componentCount (2 bytes)        │  ← Entity 1 header
// │   ├─ componentTypeId (2) │ dataSize (2) │ data (N)     │  ← Component 0
// │   └─ ...                                               │
// ├─────────────────────────────────────────────────────────┤
// │ ...more entities...                                     │
// └─────────────────────────────────────────────────────────┘

void broadcastState(ENetHost* server, entt::registry& registry) {
    auto view = registry.view<shared::Entity>();
    std::vector<uint8_t> buffer;

    uint16_t entityCount = 0;
    // reserve space for header
    size_t headerPos = buffer.size();
    buffer.resize(buffer.size() + sizeof(shared::PacketType) + sizeof(uint16_t));

    for (auto ent : view) {
        uint32_t entityId = registry.get<shared::Entity>(ent).id;
        size_t entityHeaderPos = buffer.size();
        // reserve entityId + componentCount
        buffer.resize(buffer.size() + sizeof(uint32_t) + sizeof(uint16_t));

        uint16_t compCount = 0;
        for (auto cid : shared::getSyncedComponentIds()) {
            auto& meta = shared::getComponentRegistry()[cid];
            size_t before = buffer.size();
            // write componentTypeId + placeholder size
            buffer.resize(buffer.size() + sizeof(uint16_t) + sizeof(uint16_t));
            std::vector<uint8_t> compBuf;
            if (meta.serialize(registry, ent, compBuf)) {
                uint16_t dataSize = static_cast<uint16_t>(compBuf.size());
                std::memcpy(&buffer[before], &cid, sizeof(uint16_t));
                std::memcpy(&buffer[before + sizeof(uint16_t)], &dataSize, sizeof(uint16_t));
                buffer.insert(buffer.end(), compBuf.begin(), compBuf.end());
                compCount++;
            } else {
                buffer.resize(before); // entity doesn't have this component, roll back
            }
        }

        std::memcpy(&buffer[entityHeaderPos], &entityId, sizeof(uint32_t));
        std::memcpy(&buffer[entityHeaderPos + sizeof(uint32_t)], &compCount, sizeof(uint16_t));
        entityCount++;
    }

    auto packetType = shared::PacketType::UPDATE_ENTITY;
    std::memcpy(&buffer[headerPos], &packetType, sizeof(shared::PacketType));
    std::memcpy(&buffer[headerPos + sizeof(shared::PacketType)], &entityCount, sizeof(uint16_t));

    ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, 1, packet);
}
