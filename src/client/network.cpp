#include "network.h"
#include "shared/protocol.h"
#include "shared/component_registry.h"
#include <cstring>
#include "global.h"

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
void update_entity_system(ENetPacket * packet) {
    auto data = packet->data;
    size_t offset = sizeof(shared::PacketType);
    uint16_t entityCount;
    std::memcpy(&entityCount, data + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);
    for (uint16_t i = 0; i < entityCount; i++) {
        uint32_t entityId;
        std::memcpy(&entityId, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        uint16_t compCount;
        std::memcpy(&compCount, data + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);
        auto it = entityMap.find(entityId);
        for (uint16_t c = 0; c < compCount; c++) {
            shared::ComponentTypeId cid;
            std::memcpy(&cid, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            uint16_t dataSize;
            std::memcpy(&dataSize, data + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            if (it != entityMap.end()) {
                auto& meta = shared::getComponentRegistry()[cid];
                meta.deserialize(registry, it->second, data + offset, dataSize);
            }
            offset += dataSize;
        }
    }
}