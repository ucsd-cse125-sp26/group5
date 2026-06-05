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
  CREDITS_MUSIC,
  // Section ambient music — one per season
  SECTION_WINTER_AMBIENT,
  SECTION_FALL_AMBIENT,
  SECTION_SUMMER_AMBIENT,
  SECTION_SPRING_AMBIENT,
  SECTION_AFTER_SPRING_AMBIENT,
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

// Overworld seasonal loop playback (client AudioEngine).
// Each season track is one file looped continuously (setLooping).
namespace music_config {
// AfterSpring.wav: start / loop from this timestamp (seconds).
// 1:12 => 72. Edit here if you re-export the WAV.
inline constexpr float kAfterSpringLoopStartSeconds = 72.0f;
inline constexpr float kSeasonMusicVolume = 0.35f;
}  // namespace music_config

}  // namespace shared
