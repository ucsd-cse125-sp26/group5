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

// 2D Minigame keys (from branch)
constexpr InputKeys KEY_MINIGAME_UP = 1 << 13;
constexpr InputKeys KEY_MINIGAME_DOWN = 1 << 14;
constexpr InputKeys KEY_MINIGAME_LEFT = 1 << 15;
constexpr InputKeys KEY_MINIGAME_RIGHT = 1 << 16;
constexpr InputKeys KEY_START_2D_MINIGAME = 1 << 17;
constexpr InputKeys KEY_STOP_2D_MINIGAME = 1 << 18;

// Maze minigame (from main)
constexpr InputKeys KEY_SPIRIT_UP = 1 << 19;
constexpr InputKeys KEY_SPIRIT_DOWN = 1 << 20;
constexpr InputKeys KEY_SPIRIT_LEFT = 1 << 21;
constexpr InputKeys KEY_SPIRIT_RIGHT = 1 << 22;
constexpr InputKeys KEY_MAZE_COLLECT = 1 << 23;
