#pragma once
#include <cstdint>

namespace shared {

enum class SoundId : uint32_t {
  JUMP = 0,
  LAND,
  FOOTSTEP,
  AMBIENT_HUM,
  ENTITY_SPEAK,
  OVERWORLD_MUSIC,
  MAZE_MUSIC,
  // Section ambient music — one per season
  SECTION_WINTER_AMBIENT,
  SECTION_FALL_AMBIENT,
  SECTION_SUMMER_AMBIENT,
  SECTION_SPRING_AMBIENT,
  // Puzzle event sounds
  PUZZLE_SWITCH_FLIP,
  PUZZLE_DOOR_OPEN,
  PUZZLE_SOLVED,
  PUZZLE_FAILED,

  // footsteps
  FOOTSTEP_1,
  FOOTSTEP_2,
  FOOTSTEP_3,
  FOOTSTEP_4,
};

}