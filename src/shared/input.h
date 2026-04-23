#pragma once

#include <cstdint>

using InputKeys = uint32_t;
constexpr InputKeys KEY_FORWARD = 1 << 0;
constexpr InputKeys KEY_BACKWARD = 1 << 1;
constexpr InputKeys KEY_LEFT = 1 << 2;
constexpr InputKeys KEY_RIGHT = 1 << 3;
constexpr InputKeys KEY_JUMP = 1 << 4;
constexpr InputKeys KEY_SWAP_MODEL = 1 << 5;
constexpr InputKeys KEY_MODEL_SMALLER = 1 << 6;
constexpr InputKeys KEY_MODEL_BIGGER = 1 << 7;
constexpr InputKeys KEY_LIGHT_DIM = 1 << 8;
constexpr InputKeys KEY_LIGHT_BRIGHT = 1 << 9;
