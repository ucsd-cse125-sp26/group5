#pragma once
#include <cstdint>

namespace shared {

    enum class SoundId : uint32_t {
        JUMP = 0,
        LAND,
        FOOTSTEP,
        MUSIC_OVERWORLD,
        MUSIC_MAZE,
        AMBIENT_HUM,
        ENTITY_SPEAK,
    };

}  