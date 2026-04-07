#pragma once
#include <cstdint>

namespace shared {
    enum class PacketType : uint8_t {
        // input packets from client to server
        KEYBOARD_INPUT,
        // state update packets from server to client
        UPDATE_POSITION,
        SPAWN_ENTITY,
        ASSIGN_ENTITY,
        DESPAWN_ENTITY,
    };

    struct AssignPacket {
        PacketType type;
        uint32_t entityId;
    };

    struct DespawnPacket {
        PacketType type;
        uint32_t entityId;
    };

    struct SpawnPacket {
        PacketType type;
        uint32_t entityId;
        float x, y;
    };

    struct StateHeader{
        PacketType type;
        uint8_t count;
    };

    struct StateEntry {
        uint32_t entityId;
        float x, y;
    };

    struct InputPacket {
        PacketType type;
        uint8_t keys;
    };
}