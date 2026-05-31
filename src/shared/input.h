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
constexpr InputKeys KEY_CYCLE_SCENE = 1 << 10;
constexpr InputKeys KEY_ENTER_MAZE = 1 << 11;
constexpr InputKeys KEY_EXIT_MINIGAME = 1 << 12;
// Maze minigame (server reads these on maze avatars; arrow keys in client).
constexpr InputKeys KEY_SPIRIT_UP = 1 << 13;
constexpr InputKeys KEY_SPIRIT_DOWN = 1 << 14;
constexpr InputKeys KEY_SPIRIT_LEFT = 1 << 15;
constexpr InputKeys KEY_SPIRIT_RIGHT = 1 << 16;
constexpr InputKeys KEY_MAZE_COLLECT = 1 << 17;
// Tangram: rotate nearest piece (R key).
constexpr InputKeys KEY_ROTATE_PIECE = 1 << 18;
constexpr InputKeys KEY_PICKUP = 1 << 19;
// Debug: print current player position (G key).
constexpr InputKeys KEY_DEBUG_PRINT_POS = 1 << 20;
constexpr InputKeys KEY_DEBUG_COMPLETE_SECTION = 1 << 21;
constexpr InputKeys KEY_DEBUG_TOGGLE_BARRIERS = 1 << 22;
