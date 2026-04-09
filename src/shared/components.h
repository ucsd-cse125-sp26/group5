#pragma once
#include <cstdint>
namespace shared {
    struct Position {
        float x, y;
    };

    struct Velocity {
        float dx, dy;
    };

    struct Entity {
        uint32_t id;    
    };

    struct PlayerInput{
        uint8_t keys;
    };

}