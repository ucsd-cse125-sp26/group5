#pragma once
#include <cstdint>

namespace shared {
    enum class PacketType : uint8_t {
        // input packets from client to server
        KEYBOARD_INPUT,
        SPAWN_ENTITY,
        // state update packets from server to client
        UPDATE_POSITION,
    };

    struct SpawnPacket {
        uint32_t entityId;
        float x, y;
    };

    struct UpdatePacket {
        uint32_t entityId;
        float x, y;
    };

    struct InputPacket {
        PacketType type;
        uint8_t keys;
    };
}