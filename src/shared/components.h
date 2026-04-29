#pragma once
#include <cstdint>
#include <string>

#include "input.h"

namespace shared {
struct Position {
  float x, y, z;
  float qw, qx, qy, qz;
};

struct Velocity {
  float dx, dy, dz;
};

struct RenderInfo {
  std::string modelName;
  float scale;
};

struct Camera {
  float pitch;
  float ht;
};

struct Entity {
  uint32_t id;
};

struct PlayerInput {
  InputKeys keys;
  InputKeys keys_prev;
  InputKeys keys_newly_pressed;
  float mouseDx;
  float mouseDy;
};

struct PointLight {
  float px, py, pz;
  float constant;
  float linear;
  float quadratic;
  float ambientR, ambientG, ambientB;
  float diffuseR, diffuseG, diffuseB;
  float specularR, specularG, specularB;
};

struct Scene {
  std::string name;
};

// struct PhysicsBody {
//   JPH::BodyID bodyId;
// };
struct PhysicsBody {
  uint32_t bodyId;
};

enum class RunPhase : uint8_t { LOBBY, INPROGRESS, FINISHED };

enum class Outcome : uint8_t { UNDECIDED, WIN, LOSE };

enum class SectionSeasonMap : uint8_t { WINTER, FALL, SUMMER, SPRING };

struct RunState {
  RunPhase phase = RunPhase::LOBBY;
  Outcome outcome = Outcome::UNDECIDED;
};

struct GameSection {
  SectionSeasonMap currentActiveSeason = SectionSeasonMap::WINTER;
  uint8_t sectionsCompleted = 0;  // count 0 to 4
};

struct PuzzleComponent {
  RunPhase phase = RunPhase::LOBBY;
  uint32_t puzzleElapsedTimeMs = 0;
  uint32_t puzzleTimeLimitMs = 0;
};

struct TimeComponent {
  uint32_t elapsedTimeMs = 0;
  uint32_t timeLimitMs = 0;
};

struct SectionController {
  SectionSeasonMap type =
      SectionSeasonMap::WINTER;  // the season map of this section
  uint32_t puzzleID = 0;         // the puzzle linked to this section
  bool unlocked = false;
  bool completed = false;
};

enum class DoorState : uint8_t {
  CLOSED = 0,
  OPENING = 1,  // Currently moving, collision is still active
  OPEN = 2,     // Fully open, players can pass
  CLOSING = 3   // Moving back down
};

struct SectionDoorComponent {
  DoorState state = DoorState::CLOSED;
  uint8_t requiredPlayers = 4;
  uint32_t sectionID = 0;  // the section this door is linked to

  // track the physical animation
  float currentZ = 0.0f;
  float targetOpenZ = -10.0f;  // where the door should end up
};

struct SwitchComponent {
  uint32_t parent =
      0;  // what entity this switch is linked to (door, puzzle, etc)
  bool switchOn = false;
};

}  // namespace shared
