#pragma once
#include <cstdint>
#include <string>

#include "input.h"

namespace shared {

// tags
struct OverworldTag {};
struct MazeTag {};

struct Position {
  float x, y, z;
  float qw, qx, qy, qz;
};

struct Velocity {
  float dx, dy, dz;
};

// Picks which animation clip a model should be playing. Empty clipName
// falls back to the first clip in the model's AnimationLibrary. The client
// drives playhead time off its own frame dt; startTickMs is reserved for a
// later authoritative-sync pass but currently unused.
struct AnimationState {
  std::string clipName;
  uint32_t startTickMs = 0;
  bool loop = true;
};

struct RenderInfo {
  std::string modelName;
  float sx = 1.0f;
  float sy = 1.0f;
  float sz = 1.0f;
  // 1-4 = join order for player avatars (shown on cube top); 0 = no slot label.
  uint8_t playerSlot = 0;
};

struct ColorBoundingBox {
  float minX = 0.0f;
  float minY = 0.0f;
  float minZ = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
  float maxZ = 0.0f;
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
  bool castsShadow = true;
};

struct DirectionalLight {
  float dirX, dirY, dirZ;
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

enum class MazeDirection : uint8_t { NONE = 0, UP, DOWN, LEFT, RIGHT };

struct MazeSpiritGrid {
  int8_t gx = 0;
  int8_t gy = 0;
};

struct MazePadBinding {
  MazeDirection pad = MazeDirection::NONE;
};

struct MazeUIState {
  bool open = false;
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
};

struct SwitchComponent {
  uint32_t parent =
      0;  // what entity this switch is linked to (door, puzzle, etc)
  bool switchOn = false;
};

struct FragmentComponent {
  SectionSeasonMap season;
  bool isPickedUp;
};

// Overworld mini-board puzzle (four players, shared green piece).
struct OverworldMazePuzzleState {
  bool active = false;
};

struct OverworldMazePiece {};
}  // namespace shared
